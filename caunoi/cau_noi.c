// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// cau-noi: chuong trinh "cau noi" nho cho app Dich Phu De Game (ban PS5).
// Dung phan loi chiaki-ng (chiaki-lib) de ket noi PS5 qua Remote Play,
// khong co giao dien, khong am thanh, chi lay hinh.
//
// Lenh:
//   cau-noi tim [--giay N]
//       Tim PS4/PS5 trong mang nha, in moi may 1 dong JSON.
//   cau-noi chup --host IP --regist-key HEX --morning HEX --out anh.bmp [--ps4] [--giay N]
//       Ket noi PS5, cho hinh on dinh, luu 1 khung hinh ra file BMP roi ngat.
//   cau-noi dang_ky --host IP --account HEX16 --pin 12345678 [--ps4]
//       Ghep doi (Link Device) voi PS5 bang ma 8 so tren PS5. In 1 dong JSON:
//       {"ok":true,"regist_key":HEX,"morning":HEX,"ten":...,"mac":HEX} hoac {"ok":false,"loi":...}

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
static void ngu_ms(unsigned ms) { Sleep(ms); }
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
static void ngu_ms(unsigned ms) { usleep(ms * 1000); }
#endif

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <chiaki/discovery.h>
#include <chiaki/session.h>
#include <chiaki/ffmpegdecoder.h>
#include <chiaki/regist.h>

#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>


static void log_ra_stderr(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)user;
	const char *muc = "I";
	switch(level)
	{
		case CHIAKI_LOG_ERROR: muc = "E"; break;
		case CHIAKI_LOG_WARNING: muc = "W"; break;
		case CHIAKI_LOG_DEBUG: muc = "D"; break;
		case CHIAKI_LOG_VERBOSE: muc = "V"; break;
		default: break;
	}
	fprintf(stderr, "[%s] %s\n", muc, msg);
	fflush(stderr);
}

static int g_fps = 30;                       // 17/10: --fps 60 -> xin PS5 phat 60 hinh/giay

static const char *lay_tham_so(int argc, char **argv, const char *ten)
{
	for(int i = 0; i < argc - 1; i++)
		if(strcmp(argv[i], ten) == 0)
			return argv[i + 1];
	return NULL;
}

static bool co_co(int argc, char **argv, const char *ten)
{
	for(int i = 0; i < argc; i++)
		if(strcmp(argv[i], ten) == 0)
			return true;
	return false;
}

static void in_json_chuoi(const char *s)
{
	putchar('"');
	for(; s && *s; s++)
	{
		if(*s == '"' || *s == '\\')
			putchar('\\');
		if((unsigned char)*s < 0x20)
			continue;
		putchar(*s);
	}
	putchar('"');
}

// ---------------------------------------------------------------- tim

static void tim_cb(ChiakiDiscoveryHost *host, void *user)
{
	(void)user;
	printf("{\"ip\":");
	in_json_chuoi(host->host_addr);
	printf(",\"ten\":");
	in_json_chuoi(host->host_name);
	printf(",\"loai\":");
	in_json_chuoi(host->host_type);
	printf(",\"id\":");
	in_json_chuoi(host->host_id);
	printf(",\"trang_thai\":");
	in_json_chuoi(chiaki_discovery_host_state_string(host->state));
	printf(",\"ps5\":%s}\n", chiaki_discovery_host_is_ps5(host) ? "true" : "false");
	fflush(stdout);
}

static int lenh_tim(ChiakiLog *log, int argc, char **argv)
{
	const char *giay_s = lay_tham_so(argc, argv, "--giay");
	unsigned giay = giay_s ? (unsigned)atoi(giay_s) : 3;
	if(giay < 1)
		giay = 1;

	ChiakiDiscovery discovery;
	if(chiaki_discovery_init(&discovery, log, AF_INET) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Khong khoi tao duoc bo tim may");
		return 2;
	}

	ChiakiDiscoveryThread thread;
	if(chiaki_discovery_thread_start(&thread, &discovery, tim_cb, NULL) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Khong chay duoc luong tim may");
		chiaki_discovery_fini(&discovery);
		return 2;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	ChiakiDiscoveryPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;

	for(unsigned lan = 0; lan < giay; lan++)
	{
		packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
		addr.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
		chiaki_discovery_send(&discovery, &packet, (struct sockaddr *)&addr, sizeof(addr));
		packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
		addr.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
		chiaki_discovery_send(&discovery, &packet, (struct sockaddr *)&addr, sizeof(addr));
		ngu_ms(1000);
	}

	chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);
	return 0;
}

// ---------------------------------------------------------------- chup

typedef struct
{
	ChiakiLog *log;
	ChiakiFfmpegDecoder decoder;
	volatile bool co_hinh_moi;
	volatile bool da_ket_noi;
	volatile bool da_thoat;
	char ly_do_thoat[256];
} ChupCtx;

static void hinh_moi_cb(ChiakiFfmpegDecoder *decoder, void *user)
{
	(void)decoder;
	ChupCtx *ctx = user;
	ctx->co_hinh_moi = true;
}

static void su_kien_cb(ChiakiEvent *event, void *user)
{
	ChupCtx *ctx = user;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			CHIAKI_LOGI(ctx->log, "Da ket noi PS5");
			ctx->da_ket_noi = true;
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			CHIAKI_LOGE(ctx->log, "PS5 dang bat ma PIN dang nhap Remote Play - chua ho tro");
			ctx->da_thoat = true;
			snprintf(ctx->ly_do_thoat, sizeof(ctx->ly_do_thoat), "PS5 doi ma PIN dang nhap");
			break;
		case CHIAKI_EVENT_QUIT:
			snprintf(ctx->ly_do_thoat, sizeof(ctx->ly_do_thoat), "%s%s%s",
					chiaki_quit_reason_string(event->quit.reason),
					event->quit.reason_str ? " - " : "",
					event->quit.reason_str ? event->quit.reason_str : "");
			CHIAKI_LOGI(ctx->log, "Phien ket thuc: %s", ctx->ly_do_thoat);
			ctx->da_thoat = true;
			break;
		default:
			break;
	}
}

static void khong_hien_cb(void *user, bool khong_hien)
{
	ChupCtx *ctx = user;
	// PS5 dang mo noi dung duoc bao ve (YouTube, Netflix, ...): Sony khong cho chia se hinh qua Remote Play
	fprintf(stderr, "TT|%s\n", khong_hien ? "bi_chan_hinh" : "het_bi_chan_hinh");
	fflush(stderr);
	if(ctx)
		CHIAKI_LOGW(ctx->log, khong_hien ? "PS5 dang chan hinh (noi dung duoc bao ve)" : "PS5 het chan hinh");
}

static void gan_bo_nhan(ChiakiSession *session, ChupCtx *ctx)
{
	ChiakiCtrlDisplaySink sink;
	memset(&sink, 0, sizeof(sink));
	sink.user = ctx;
	sink.cantdisplay_cb = khong_hien_cb;
	chiaki_session_ctrl_set_display_sink(session, &sink);
	chiaki_session_set_event_cb(session, su_kien_cb, ctx);
	chiaki_session_set_video_sample_cb(session, chiaki_ffmpeg_decoder_video_sample_cb, &ctx->decoder);
}

static int hex_sang_byte(const char *hex, uint8_t *out, size_t out_size)
{
	size_t n = strlen(hex);
	if(n != out_size * 2)
		return -1;
	for(size_t i = 0; i < out_size; i++)
	{
		unsigned v;
		if(sscanf(hex + i * 2, "%2x", &v) != 1)
			return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static uint8_t kep(int v)
{
	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static bool dinh_dang_ho_tro(const AVFrame *f)
{
	return f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P || f->format == AV_PIX_FMT_NV12;
}

// Doi AVFrame (yuv420p / yuvj420p / nv12) sang BGR 24-bit, hang tu tren xuong, moi hang w*3 byte
static void chuyen_bgr(const AVFrame *f, uint8_t *out)
{
	int w = f->width, h = f->height;
	bool nv12 = f->format == AV_PIX_FMT_NV12;
	bool full_range = f->format == AV_PIX_FMT_YUVJ420P || f->color_range == AVCOL_RANGE_JPEG;
	for(int y = 0; y < h; y++)
	{
		const uint8_t *py = f->data[0] + (size_t)y * f->linesize[0];
		uint8_t *o = out + (size_t)y * w * 3;
		for(int x = 0; x < w; x++)
		{
			int Y = py[x], U, V;
			if(nv12)
			{
				const uint8_t *puv = f->data[1] + (size_t)(y / 2) * f->linesize[1];
				U = puv[(x / 2) * 2];
				V = puv[(x / 2) * 2 + 1];
			}
			else
			{
				U = f->data[1][(size_t)(y / 2) * f->linesize[1] + x / 2];
				V = f->data[2][(size_t)(y / 2) * f->linesize[2] + x / 2];
			}
			int c = full_range ? Y : (Y - 16) * 298 / 256;
			int d = U - 128, e = V - 128;
			// BT.709 (hinh HD)
			int r = c + (full_range ? (403 * e) / 256 : (459 * e) / 256);
			int g = c - (full_range ? (48 * d + 120 * e) / 256 : (55 * d + 136 * e) / 256);
			int b = c + (full_range ? (475 * d) / 256 : (541 * d) / 256);
			o[x * 3 + 0] = kep(b);
			o[x * 3 + 1] = kep(g);
			o[x * 3 + 2] = kep(r);
		}
	}
}

// Ghi AVFrame ra BMP 24-bit
static int ghi_bmp(ChiakiLog *log, AVFrame *f, const char *duong_dan)
{
	int w = f->width, h = f->height;
	if(!dinh_dang_ho_tro(f))
	{
		CHIAKI_LOGE(log, "Dinh dang hinh chua ho tro: %d", f->format);
		return -1;
	}
	uint8_t *bgr = malloc((size_t)w * h * 3);
	FILE *fp = fopen(duong_dan, "wb");
	if(!fp || !bgr)
	{
		CHIAKI_LOGE(log, "Khong mo duoc file de ghi: %s", duong_dan);
		free(bgr);
		if(fp)
			fclose(fp);
		return -1;
	}
	chuyen_bgr(f, bgr);
	int dong = (w * 3 + 3) & ~3;
	uint32_t kich_thuoc_anh = (uint32_t)dong * (uint32_t)h;
	uint8_t hdr[54] = { 'B', 'M' };
	uint32_t tong = 54 + kich_thuoc_anh;
	memcpy(hdr + 2, &tong, 4);
	uint32_t off = 54; memcpy(hdr + 10, &off, 4);
	uint32_t dib = 40; memcpy(hdr + 14, &dib, 4);
	int32_t ww = w, hh = h; memcpy(hdr + 18, &ww, 4); memcpy(hdr + 22, &hh, 4);
	uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
	uint16_t bpp = 24; memcpy(hdr + 28, &bpp, 2);
	memcpy(hdr + 34, &kich_thuoc_anh, 4);
	fwrite(hdr, 1, 54, fp);
	uint8_t dem[4] = { 0 };
	for(int y = h - 1; y >= 0; y--)
	{
		fwrite(bgr + (size_t)y * w * 3, 1, (size_t)w * 3, fp);
		fwrite(dem, 1, (size_t)(dong - w * 3), fp);
	}
	fclose(fp);
	free(bgr);
	CHIAKI_LOGI(log, "Da luu hinh %dx%d vao %s", w, h, duong_dan);
	return 0;
}

static int tao_ket_noi(ChiakiConnectInfo *ci, const char *host, const char *regist_hex, const char *morning_hex, bool ps5)
{
	memset(ci, 0, sizeof(*ci));
	ci->ps5 = ps5;
	ci->host = host;
	if(hex_sang_byte(regist_hex, (uint8_t *)ci->regist_key, sizeof(ci->regist_key)) != 0
		|| hex_sang_byte(morning_hex, ci->morning, sizeof(ci->morning)) != 0)
	{
		fprintf(stderr, "regist-key / morning phai la 32 ky tu hex\n");
		return 1;
	}
	chiaki_connect_video_profile_preset(&ci->video_profile, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
		g_fps >= 60 ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30);
	ci->video_profile.codec = CHIAKI_CODEC_H264;
	ci->video_profile_auto_downgrade = true;
	ci->enable_keyboard = false;
	ci->enable_dualsense = false;
	ci->audio_video_disabled = CHIAKI_AUDIO_DISABLED;
	ci->auto_regist = false;
	ci->holepunch_session = NULL;
	ci->packet_loss_max = 0.05;
	ci->enable_idr_on_fec_failure = true;
	return 0;
}

static int lenh_chup(ChiakiLog *log, int argc, char **argv)
{
	const char *host = lay_tham_so(argc, argv, "--host");
	const char *regist_hex = lay_tham_so(argc, argv, "--regist-key");
	const char *morning_hex = lay_tham_so(argc, argv, "--morning");
	const char *out = lay_tham_so(argc, argv, "--out");
	const char *giay_s = lay_tham_so(argc, argv, "--giay");
	bool ps5 = !co_co(argc, argv, "--ps4");
	unsigned giay_cho = giay_s ? (unsigned)atoi(giay_s) : 3;
	if(!host || !regist_hex || !morning_hex || !out)
	{
		fprintf(stderr, "Thieu tham so. Can: --host --regist-key --morning --out\n");
		return 1;
	}

	ChiakiConnectInfo ci;
	if(tao_ket_noi(&ci, host, regist_hex, morning_hex, ps5) != 0)
		return 1;

	static ChupCtx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.log = log;

	if(chiaki_ffmpeg_decoder_init(&ctx.decoder, log, CHIAKI_CODEC_H264, g_fps, NULL, NULL, hinh_moi_cb, &ctx) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Khong khoi tao duoc bo giai ma hinh");
		return 2;
	}

	static ChiakiSession session;
	ChiakiErrorCode err = chiaki_session_init(&session, &ci, log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Khoi tao phien that bai: %s", chiaki_error_string(err));
		chiaki_ffmpeg_decoder_fini(&ctx.decoder);
		return 2;
	}
	gan_bo_nhan(&session, &ctx);

	err = chiaki_session_start(&session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Khong bat dau duoc phien: %s", chiaki_error_string(err));
		chiaki_session_fini(&session);
		chiaki_ffmpeg_decoder_fini(&ctx.decoder);
		return 2;
	}

	int ket_qua = 3;
	unsigned ms_tu_hinh_dau = 0;
	bool da_co_hinh = false;
	for(unsigned ms = 0; ms < 40000 && !ctx.da_thoat; ms += 50)
	{
		ngu_ms(50);
		if(da_co_hinh)
			ms_tu_hinh_dau += 50;
		if(!ctx.co_hinh_moi)
			continue;
		ctx.co_hinh_moi = false;
		int32_t mat = 0;
		ChiakiFfmpegFrame fr = chiaki_ffmpeg_decoder_pull_frame(&ctx.decoder, &mat);
		if(!fr.frame)
			continue;
		if(!da_co_hinh)
		{
			CHIAKI_LOGI(log, "Nhan duoc hinh dau tien %dx%d, cho %u giay cho hinh on dinh", fr.frame->width, fr.frame->height, giay_cho);
			da_co_hinh = true;
		}
		if(ms_tu_hinh_dau >= giay_cho * 1000)
		{
			ket_qua = ghi_bmp(log, fr.frame, out) == 0 ? 0 : 4;
			av_frame_free(&fr.frame);
			break;
		}
		av_frame_free(&fr.frame);
	}
	if(ket_qua == 3)
		CHIAKI_LOGE(log, "Khong lay duoc hinh. %s", ctx.ly_do_thoat[0] ? ctx.ly_do_thoat : "Het thoi gian cho (40 giay)");

	chiaki_session_stop(&session);
	chiaki_session_join(&session);
	chiaki_session_fini(&session);
	chiaki_ffmpeg_decoder_fini(&ctx.decoder);
	return ket_qua;
}


// ---------------------------------------------------------------- phat (chay lien tuc)
//
// stdin  (dong lenh): "H" = xin hinh moi nhat, "Q" = thoat (EOF cung la thoat)
// stdout (nhi phan) : moi lan "H" tra ve 16 byte dau "CNHI" + rong(u32) + cao(u32) + so_thu_tu(u32),
//                     sau do rong*cao*3 byte BGR (hang tu tren xuong). Chua co hinh -> rong=cao=0.
// stderr            : nhat ky + dong trang thai "TT|dang_ket_noi" / "TT|da_ket_noi" / "TT|mat_ket_noi|ly do"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static volatile int phat_yeu_cau = 0;
static volatile int phat_yeu_cau_nho = 0;   // 17/10: "N" = hinh THU NHO 1/2 (cua so xem, nhe 4 lan)
static volatile bool phat_dung = false;

static void *doc_lenh_thread(void *user)
{
	(void)user;
	char dong[64];
	while(fgets(dong, sizeof(dong), stdin))
	{
		if(dong[0] == 'H' || dong[0] == 'h')
			phat_yeu_cau++;
		else if(dong[0] == 'N' || dong[0] == 'n')
			phat_yeu_cau_nho++;
		else if(dong[0] == 'Q' || dong[0] == 'q')
			break;
	}
	phat_dung = true;
	return NULL;
}

static void trang_thai(const char *tt, const char *ly_do)
{
	fprintf(stderr, "TT|%s%s%s\n", tt, ly_do ? "|" : "", ly_do ? ly_do : "");
	fflush(stderr);
}

static void gui_hinh(AVFrame *f, uint32_t so, uint8_t **bgr, size_t *bgr_cap)
{
	uint32_t w = 0, h = 0;
	if(f && dinh_dang_ho_tro(f))
	{
		w = (uint32_t)f->width;
		h = (uint32_t)f->height;
		size_t can = (size_t)w * h * 3;
		if(*bgr_cap < can)
		{
			free(*bgr);
			*bgr = malloc(can);
			*bgr_cap = *bgr ? can : 0;
		}
		if(!*bgr)
			w = h = 0;
		else
			chuyen_bgr(f, *bgr);
	}
	uint8_t hdr[16] = { 'C', 'N', 'H', 'I' };
	memcpy(hdr + 4, &w, 4);
	memcpy(hdr + 8, &h, 4);
	memcpy(hdr + 12, &so, 4);
	fwrite(hdr, 1, 16, stdout);
	if(w && h)
		fwrite(*bgr, 1, (size_t)w * h * 3, stdout);
	fflush(stdout);
}

// 17/10: gui hinh thu nho 1/2 (trung binh 2x2) - cua so xem hinh khong can 1080p, nhe 4 lan -> 60 hinh/giay
static void gui_hinh_nho(AVFrame *f, uint32_t so, uint8_t **bgr, size_t *bgr_cap, uint8_t **nho, size_t *nho_cap)
{
	uint32_t w = 0, h = 0;
	if(f && dinh_dang_ho_tro(f))
	{
		uint32_t W = (uint32_t)f->width, H = (uint32_t)f->height;
		size_t can = (size_t)W * H * 3;
		if(*bgr_cap < can)
		{
			free(*bgr);
			*bgr = malloc(can);
			*bgr_cap = *bgr ? can : 0;
		}
		w = W / 2;
		h = H / 2;
		size_t can_nho = (size_t)w * h * 3;
		if(*nho_cap < can_nho)
		{
			free(*nho);
			*nho = malloc(can_nho);
			*nho_cap = *nho ? can_nho : 0;
		}
		if(!*bgr || !*nho || !w || !h)
			w = h = 0;
		else
		{
			chuyen_bgr(f, *bgr);
			const uint8_t *g = *bgr;
			uint8_t *n = *nho;
			for(uint32_t y = 0; y < h; y++)
			{
				const uint8_t *r0 = g + (size_t)(2 * y) * W * 3;
				const uint8_t *r1 = r0 + (size_t)W * 3;
				uint8_t *o = n + (size_t)y * w * 3;
				for(uint32_t x = 0; x < w; x++)
				{
					const uint8_t *a = r0 + (size_t)x * 6, *b = r1 + (size_t)x * 6;
					o[0] = (uint8_t)((a[0] + a[3] + b[0] + b[3] + 2) >> 2);
					o[1] = (uint8_t)((a[1] + a[4] + b[1] + b[4] + 2) >> 2);
					o[2] = (uint8_t)((a[2] + a[5] + b[2] + b[5] + 2) >> 2);
					o += 3;
				}
			}
		}
	}
	uint8_t hdr[16] = { 'C', 'N', 'H', 'I' };
	memcpy(hdr + 4, &w, 4);
	memcpy(hdr + 8, &h, 4);
	memcpy(hdr + 12, &so, 4);
	fwrite(hdr, 1, 16, stdout);
	if(w && h)
		fwrite(*nho, 1, (size_t)w * h * 3, stdout);
	fflush(stdout);
}

static int lenh_phat(ChiakiLog *log, int argc, char **argv)
{
	const char *host = lay_tham_so(argc, argv, "--host");
	const char *regist_hex = lay_tham_so(argc, argv, "--regist-key");
	const char *morning_hex = lay_tham_so(argc, argv, "--morning");
	bool ps5 = !co_co(argc, argv, "--ps4");
	if(!host || !regist_hex || !morning_hex)
	{
		fprintf(stderr, "Thieu tham so. Can: --host --regist-key --morning\n");
		return 1;
	}
	ChiakiConnectInfo ci;
	if(tao_ket_noi(&ci, host, regist_hex, morning_hex, ps5) != 0)
		return 1;

#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	ChiakiThread th_lenh;
	if(chiaki_thread_create(&th_lenh, doc_lenh_thread, NULL) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "Khong tao duoc luong doc lenh");
		return 2;
	}

	static ChupCtx ctx;
	static ChiakiSession session;
	uint8_t *bgr = NULL;
	size_t bgr_cap = 0;
	uint8_t *nho = NULL;
	size_t nho_cap = 0;
	uint32_t so_hinh = 0;
	unsigned lan_thu = 0;

	while(!phat_dung)
	{
		memset(&ctx, 0, sizeof(ctx));
		ctx.log = log;
		AVFrame *hinh_moi_nhat = NULL;
		trang_thai("dang_ket_noi", NULL);

		if(chiaki_ffmpeg_decoder_init(&ctx.decoder, log, CHIAKI_CODEC_H264, g_fps, NULL, NULL, hinh_moi_cb, &ctx) != CHIAKI_ERR_SUCCESS)
		{
			trang_thai("mat_ket_noi", "khong khoi tao duoc bo giai ma");
			break;
		}
		bool co_phien = false;
		ChiakiErrorCode err = chiaki_session_init(&session, &ci, log);
		if(err == CHIAKI_ERR_SUCCESS)
		{
			co_phien = true;
			gan_bo_nhan(&session, &ctx);
			err = chiaki_session_start(&session);
		}
		if(err != CHIAKI_ERR_SUCCESS)
		{
			snprintf(ctx.ly_do_thoat, sizeof(ctx.ly_do_thoat), "%s", chiaki_error_string(err));
			ctx.da_thoat = true;
		}

		bool da_bao_ket_noi = false;
		while(!phat_dung && !ctx.da_thoat)
		{
			if(ctx.da_ket_noi && !da_bao_ket_noi)
			{
				trang_thai("da_ket_noi", NULL);
				da_bao_ket_noi = true;
				lan_thu = 0;
			}
			if(ctx.co_hinh_moi)
			{
				ctx.co_hinh_moi = false;
				int32_t mat = 0;
				ChiakiFfmpegFrame fr = chiaki_ffmpeg_decoder_pull_frame(&ctx.decoder, &mat);
				if(fr.frame)
				{
					if(hinh_moi_nhat)
						av_frame_free(&hinh_moi_nhat);
					hinh_moi_nhat = fr.frame;
					so_hinh++;
				}
			}
			while(phat_yeu_cau > 0 || phat_yeu_cau_nho > 0)
			{
				if(phat_yeu_cau > 0)
				{
					phat_yeu_cau--;
					gui_hinh(hinh_moi_nhat, so_hinh, &bgr, &bgr_cap);
				}
				else
				{
					phat_yeu_cau_nho--;
					gui_hinh_nho(hinh_moi_nhat, so_hinh, &bgr, &bgr_cap, &nho, &nho_cap);
				}
			}
			ngu_ms(5);
		}

		if(co_phien)
		{
			chiaki_session_stop(&session);
			chiaki_session_join(&session);
			chiaki_session_fini(&session);
		}
		chiaki_ffmpeg_decoder_fini(&ctx.decoder);
		if(hinh_moi_nhat)
			av_frame_free(&hinh_moi_nhat);

		if(phat_dung)
			break;
		trang_thai("mat_ket_noi", ctx.ly_do_thoat[0] ? ctx.ly_do_thoat : "khong ro");
		// cho roi ket noi lai (2s, 4s, ... toi da 10s); van tra loi xin hinh (rong) trong luc cho
		lan_thu++;
		unsigned cho_ms = lan_thu * 2000 > 10000 ? 10000 : lan_thu * 2000;
		for(unsigned ms = 0; ms < cho_ms && !phat_dung; ms += 20)
		{
			while(phat_yeu_cau > 0)
			{
				phat_yeu_cau--;
				gui_hinh(NULL, so_hinh, &bgr, &bgr_cap);
			}
			ngu_ms(20);
		}
	}

	free(bgr);
	free(nho);
	trang_thai("da_dung", NULL);
	return 0;
}

// ---------------------------------------------------------------- dang ky (ghep doi)

typedef struct
{
	ChiakiMutex mutex;
	ChiakiCond cond;
	bool xong;
	ChiakiRegistEventType kieu;
	ChiakiRegisteredHost may;
} DangKy;

static void dang_ky_cb(ChiakiRegistEvent *ev, void *user)
{
	DangKy *dk = user;
	chiaki_mutex_lock(&dk->mutex);
	dk->kieu = ev->type;
	if(ev->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && ev->registered_host)
		dk->may = *ev->registered_host;
	dk->xong = true;
	chiaki_mutex_unlock(&dk->mutex);
	chiaki_cond_signal(&dk->cond);
}

static void in_hex(const uint8_t *b, size_t n)
{
	for(size_t i = 0; i < n; i++)
		printf("%02x", b[i]);
}

static int lenh_dang_ky(ChiakiLog *log, int argc, char **argv)
{
	const char *host = lay_tham_so(argc, argv, "--host");
	const char *account_hex = lay_tham_so(argc, argv, "--account");
	const char *pin_s = lay_tham_so(argc, argv, "--pin");
	bool ps4 = co_co(argc, argv, "--ps4");
	if(!host || !account_hex || !pin_s)
	{
		printf("{\"ok\":false,\"loi\":\"thieu tham so\"}\n");
		return 1;
	}
	ChiakiRegistInfo info;
	memset(&info, 0, sizeof(info));
	info.target = ps4 ? CHIAKI_TARGET_PS4_10 : CHIAKI_TARGET_PS5_1;
	info.host = host;
	info.broadcast = false;
	info.psn_online_id = NULL;
	if(hex_sang_byte(account_hex, info.psn_account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE) != 0)
	{
		printf("{\"ok\":false,\"loi\":\"account sai\"}\n");
		return 1;
	}
	info.pin = (uint32_t)strtoul(pin_s, NULL, 10);
	info.console_pin = 0;
	info.holepunch_info = NULL;
	info.rudp = NULL;

	DangKy dk;
	memset(&dk, 0, sizeof(dk));
	chiaki_mutex_init(&dk.mutex, false);
	chiaki_cond_init(&dk.cond);
	ChiakiRegist regist;
	if(chiaki_regist_start(&regist, log, &info, dang_ky_cb, &dk) != CHIAKI_ERR_SUCCESS)
	{
		printf("{\"ok\":false,\"loi\":\"khong bat dau duoc\"}\n");
		return 2;
	}
	chiaki_mutex_lock(&dk.mutex);
	for(int i = 0; i < 300 && !dk.xong; i++)          // toi da ~30 giay
		chiaki_cond_timedwait(&dk.cond, &dk.mutex, 100);
	bool xong = dk.xong;
	chiaki_mutex_unlock(&dk.mutex);
	if(!xong)
		chiaki_regist_stop(&regist);
	chiaki_regist_fini(&regist);

	if(xong && dk.kieu == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS)
	{
		printf("{\"ok\":true,\"regist_key\":\"");
		in_hex((const uint8_t *)dk.may.rp_regist_key, sizeof(dk.may.rp_regist_key));
		printf("\",\"morning\":\"");
		in_hex(dk.may.rp_key, sizeof(dk.may.rp_key));
		printf("\",\"ten\":");
		dk.may.server_nickname[sizeof(dk.may.server_nickname) - 1] = 0;
		in_json_chuoi(dk.may.server_nickname);
		printf(",\"mac\":\"");
		in_hex(dk.may.server_mac, sizeof(dk.may.server_mac));
		printf("\",\"target\":%d}\n", (int)dk.may.target);
		fflush(stdout);
		return 0;
	}
	printf("{\"ok\":false,\"loi\":\"%s\"}\n", !xong ? "het gio" :
		(dk.kieu == CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED ? "huy" : "that bai"));
	fflush(stdout);
	return 3;
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		fprintf(stderr,
				"cau-noi (dua tren chiaki-ng, AGPL-3.0)\n"
				"  cau-noi tim [--giay N]\n"
				"  cau-noi chup --host IP --regist-key HEX --morning HEX --out anh.bmp [--ps4] [--giay N]\n"
				"  cau-noi phat --host IP --regist-key HEX --morning HEX [--ps4] [--fps 60]   (stdin: H=hinh, N=hinh nho 1/2, Q=thoat)\n"
				"  cau-noi dang_ky --host IP --account HEX16 --pin 12345678 [--ps4]\n");
		return 1;
	}

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "chiaki_lib_init that bai: %s\n", chiaki_error_string(err));
		return 2;
	}

	ChiakiLog log;
	bool chi_tiet = co_co(argc, argv, "--chi-tiet");
	chiaki_log_init(&log, chi_tiet ? CHIAKI_LOG_ALL : (CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR), log_ra_stderr, NULL);

	const char *fps_s = lay_tham_so(argc, argv, "--fps");
	if(fps_s && atoi(fps_s) >= 60)
		g_fps = 60;

	if(strcmp(argv[1], "tim") == 0)
		return lenh_tim(&log, argc, argv);
	if(strcmp(argv[1], "chup") == 0)
		return lenh_chup(&log, argc, argv);
	if(strcmp(argv[1], "phat") == 0)
		return lenh_phat(&log, argc, argv);
	if(strcmp(argv[1], "dang_ky") == 0)
		return lenh_dang_ky(&log, argc, argv);

	fprintf(stderr, "Lenh khong ro: %s\n", argv[1]);
	return 1;
}
