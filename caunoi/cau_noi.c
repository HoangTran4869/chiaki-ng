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

// Ghi AVFrame (yuv420p / yuvj420p / nv12) ra BMP 24-bit
static int ghi_bmp(ChiakiLog *log, AVFrame *f, const char *duong_dan)
{
	int w = f->width, h = f->height;
	bool nv12 = f->format == AV_PIX_FMT_NV12;
	if(!(f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P || nv12))
	{
		CHIAKI_LOGE(log, "Dinh dang hinh chua ho tro: %d", f->format);
		return -1;
	}
	int dong = (w * 3 + 3) & ~3;
	uint8_t *buf = calloc((size_t)dong, 1);
	FILE *fp = fopen(duong_dan, "wb");
	if(!fp || !buf)
	{
		CHIAKI_LOGE(log, "Khong mo duoc file de ghi: %s", duong_dan);
		free(buf);
		if(fp)
			fclose(fp);
		return -1;
	}
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

	bool full_range = f->format == AV_PIX_FMT_YUVJ420P || f->color_range == AVCOL_RANGE_JPEG;
	for(int y = h - 1; y >= 0; y--)
	{
		const uint8_t *py = f->data[0] + (size_t)y * f->linesize[0];
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
			buf[x * 3 + 0] = kep(b);
			buf[x * 3 + 1] = kep(g);
			buf[x * 3 + 2] = kep(r);
		}
		fwrite(buf, 1, (size_t)dong, fp);
	}
	fclose(fp);
	free(buf);
	CHIAKI_LOGI(log, "Da luu hinh %dx%d vao %s", w, h, duong_dan);
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
	memset(&ci, 0, sizeof(ci));
	ci.ps5 = ps5;
	ci.host = host;
	if(hex_sang_byte(regist_hex, (uint8_t *)ci.regist_key, sizeof(ci.regist_key)) != 0
		|| hex_sang_byte(morning_hex, ci.morning, sizeof(ci.morning)) != 0)
	{
		fprintf(stderr, "regist-key / morning phai la 32 ky tu hex\n");
		return 1;
	}
	chiaki_connect_video_profile_preset(&ci.video_profile, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, CHIAKI_VIDEO_FPS_PRESET_30);
	ci.video_profile.codec = CHIAKI_CODEC_H264;
	ci.video_profile_auto_downgrade = true;
	ci.enable_keyboard = false;
	ci.enable_dualsense = false;
	ci.audio_video_disabled = CHIAKI_AUDIO_DISABLED;
	ci.auto_regist = false;
	ci.holepunch_session = NULL;
	ci.packet_loss_max = 0.05;
	ci.enable_idr_on_fec_failure = true;

	static ChupCtx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.log = log;

	if(chiaki_ffmpeg_decoder_init(&ctx.decoder, log, CHIAKI_CODEC_H264, 30, NULL, NULL, hinh_moi_cb, &ctx) != CHIAKI_ERR_SUCCESS)
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
	chiaki_session_set_event_cb(&session, su_kien_cb, &ctx);
	chiaki_session_set_video_sample_cb(&session, chiaki_ffmpeg_decoder_video_sample_cb, &ctx.decoder);

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

// ---------------------------------------------------------------- main

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		fprintf(stderr,
				"cau-noi (dua tren chiaki-ng, AGPL-3.0)\n"
				"  cau-noi tim [--giay N]\n"
				"  cau-noi chup --host IP --regist-key HEX --morning HEX --out anh.bmp [--ps4] [--giay N]\n");
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

	if(strcmp(argv[1], "tim") == 0)
		return lenh_tim(&log, argc, argv);
	if(strcmp(argv[1], "chup") == 0)
		return lenh_chup(&log, argc, argv);

	fprintf(stderr, "Lenh khong ro: %s\n", argv[1]);
	return 1;
}
