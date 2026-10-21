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
#include <chiaki/time.h>

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

static volatile unsigned g_dem_giai_ma = 0;   // 17/10: dem hinh PS5 gui ve (giai ma xong) -> ghi nhat ky
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
	g_dem_giai_ma++;
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
	if(g_fps >= 60)
		ci->video_profile.bitrate = 30000;   // 17/10: 60 hinh/giay -> gap doi luong du lieu (mac dinh 15000) cho do net
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

// ---------------------------------------------------------------- 20/10: CUA SO HINH MUOT (Windows)
// App gui "V <hwnd>": cau noi tao 1 cua so CON (SDL2, ve bang card do hoa) nam trong o xem hinh cua app,
// ve thang khung YUV vua giai ma -> 1080p, 60 hinh/giay nhu chiaki (truoc: chep hinh BGR 6 MB/hinh sang Python
// roi Tkinter ve -> ~20 hinh/giay, mo). Cua so con bi TAT nhan chuot (WS_DISABLED) -> chuot roi xuong o cua app.
//   "V <hwnd>" bat / doi cha   "v" tat   "a" an tam   "A" hien lai
static volatile long long video_hwnd_moi = 0;   // >0: bat vao cua so cha nay
static volatile int video_lenh = 0;              // 1 = tat, 2 = an, 3 = hien
// 21/10 Mac: "M x y w h" = cua so hinh muot RIENG (khong vien, khong nhan chuot) dat dung cho o xem cua app
// (macOS khong cho gan cua so cua chuong trinh khac vao trong) - toa do diem man hinh, goc tren-trai
static volatile int video_m_moi = 0;
static volatile int video_mx = 0, video_my = 0, video_mw = 0, video_mh = 0;
#if defined(_WIN32) && defined(CAUNOI_VIDEO)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
typedef struct
{
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Texture *tex;
	int tex_w, tex_h, tex_nv12;
	HWND cha, con;
	int rong, cao;
	bool an;
	uint32_t so_ve;
	int lan_reset;          // 20/10: card do hoa bi reset (qua tai / doi driver) -> dem, nhieu lan thi tat
	int lan_loi_lien;       // ve / nap hinh loi lien tiep
} Video;
static int g_vd_reset_tong = 0;
static Video g_vd;
static bool g_sdl_ok = false;

static void video_tat(void)
{
	if(g_vd.tex) SDL_DestroyTexture(g_vd.tex);
	if(g_vd.ren) SDL_DestroyRenderer(g_vd.ren);
	if(g_vd.win) SDL_DestroyWindow(g_vd.win);
	memset(&g_vd, 0, sizeof(g_vd));
}

static void video_bat(HWND cha)
{
	video_tat();
	if(!g_sdl_ok)
	{
		SDL_SetMainReady();
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		if(SDL_Init(SDL_INIT_VIDEO) != 0)
		{
			fprintf(stderr, "VIDEO|loi|SDL_Init: %s\n", SDL_GetError());
			fflush(stderr);
			return;
		}
		g_sdl_ok = true;
	}
	RECT r;
	if(!IsWindow(cha) || !GetClientRect(cha, &r))
	{
		fprintf(stderr, "VIDEO|loi|cua so cha khong hop le\n");
		fflush(stderr);
		return;
	}
	g_vd.win = SDL_CreateWindow("cau-noi", 0, 0, r.right > 16 ? r.right : 16, r.bottom > 16 ? r.bottom : 16,
			SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
	if(!g_vd.win)
	{
		fprintf(stderr, "VIDEO|loi|SDL_CreateWindow: %s\n", SDL_GetError());
		fflush(stderr);
		return;
	}
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if(!SDL_GetWindowWMInfo(g_vd.win, &wm))
	{
		video_tat();
		return;
	}
	g_vd.con = wm.info.win.window;
	g_vd.cha = cha;
	SetWindowLongPtrW(g_vd.con, GWL_STYLE, WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS);
	SetWindowLongPtrW(g_vd.con, GWL_EXSTYLE, 0);
	SetParent(g_vd.con, cha);
	g_vd.rong = r.right; g_vd.cao = r.bottom;
	SetWindowPos(g_vd.con, HWND_TOP, 0, 0, r.right, r.bottom, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
	g_vd.ren = SDL_CreateRenderer(g_vd.win, -1, SDL_RENDERER_ACCELERATED);
	if(!g_vd.ren)
		g_vd.ren = SDL_CreateRenderer(g_vd.win, -1, 0);
	if(!g_vd.ren)
	{
		fprintf(stderr, "VIDEO|loi|SDL_CreateRenderer: %s\n", SDL_GetError());
		fflush(stderr);
		video_tat();
		return;
	}
	SDL_RendererInfo ri;
	SDL_GetRendererInfo(g_vd.ren, &ri);
	SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT709);
	SDL_SetRenderDrawColor(g_vd.ren, 0, 0, 0, 255);
	SDL_RenderClear(g_vd.ren);
	SDL_RenderPresent(g_vd.ren);
	fprintf(stderr, "VIDEO|bat|%s\n", ri.name ? ri.name : "?");
	fflush(stderr);
}

// moi vong lap: theo kich thuoc o cha, an / hien, ve khung moi
static void video_nhip(AVFrame *f, uint32_t so)
{
	if(video_hwnd_moi > 0)
	{
		HWND cha = (HWND)(intptr_t)video_hwnd_moi;
		video_hwnd_moi = 0;
		video_bat(cha);
	}
	int l = video_lenh;
	if(l)
	{
		video_lenh = 0;
		if(l == 1) { video_tat(); fprintf(stderr, "VIDEO|tat\n"); fflush(stderr); }
		else if(g_vd.con) { g_vd.an = (l == 2); ShowWindow(g_vd.con, g_vd.an ? SW_HIDE : SW_SHOWNA); g_vd.so_ve = 0; }
	}
	if(!g_vd.ren)
		return;
	SDL_PumpEvents();
	if(!IsWindow(g_vd.cha))
	{
		video_tat();
		return;
	}
	// 20/10 anh luu y (tung sap app khi card do hoa qua tai): card bi reset -> SDL bao su kien; tao lai bo ve,
	// reset >= 3 lan (ca phien) hoac ve loi lien tiep -> TU TAT hinh muot, bao app quay ve hinh thuong (khong sap)
	SDL_Event ev;
	bool reset = false;
	while(SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_RENDER_TARGETS_RESET, SDL_RENDER_DEVICE_RESET) > 0)
		if(ev.type == SDL_RENDER_DEVICE_RESET)
			reset = true;
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	if(reset)
	{
		HWND cha = g_vd.cha;
		g_vd_reset_tong++;
		fprintf(stderr, "[W] VIDEO: card do hoa bi reset (lan %d)\n", g_vd_reset_tong);
		fflush(stderr);
		if(g_vd_reset_tong >= 3)
		{
			video_tat();
			fprintf(stderr, "VIDEO|loi|card do hoa bi reset nhieu lan\n");
			fflush(stderr);
			return;
		}
		video_bat(cha);
		if(!g_vd.ren)
			return;
	}
	RECT r;
	bool doi = false;
	if(GetClientRect(g_vd.cha, &r) && (r.right != g_vd.rong || r.bottom != g_vd.cao))
	{
		g_vd.rong = r.right; g_vd.cao = r.bottom;
		SetWindowPos(g_vd.con, NULL, 0, 0, r.right, r.bottom, SWP_NOACTIVATE | SWP_NOZORDER);
		SDL_SetWindowSize(g_vd.win, r.right > 1 ? r.right : 1, r.bottom > 1 ? r.bottom : 1);
		doi = true;
	}
	if(g_vd.an || !f || !dinh_dang_ho_tro(f) || (so == g_vd.so_ve && !doi))
		return;
	int nv12 = f->format == AV_PIX_FMT_NV12;
	if(!g_vd.tex || g_vd.tex_w != f->width || g_vd.tex_h != f->height || g_vd.tex_nv12 != nv12)
	{
		if(g_vd.tex) SDL_DestroyTexture(g_vd.tex);
		g_vd.tex = SDL_CreateTexture(g_vd.ren, nv12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV,
				SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
		g_vd.tex_w = f->width; g_vd.tex_h = f->height; g_vd.tex_nv12 = nv12;
		if(!g_vd.tex)
			return;
	}
	int kq = nv12
		? SDL_UpdateNVTexture(g_vd.tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1])
		: SDL_UpdateYUVTexture(g_vd.tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1],
				f->data[2], f->linesize[2]);
	if(kq != 0)
	{
		if(++g_vd.lan_loi_lien >= 30)        // ~0,5 giay ve loi lien tuc -> tat, khong co ve tiep
		{
			fprintf(stderr, "VIDEO|loi|ve hinh loi lien tiep: %s\n", SDL_GetError());
			fflush(stderr);
			video_tat();
		}
		return;
	}
	g_vd.lan_loi_lien = 0;
	int ow = 0, oh = 0;
	SDL_GetRendererOutputSize(g_vd.ren, &ow, &oh);
	double tl = (double)ow / f->width < (double)oh / f->height ? (double)ow / f->width : (double)oh / f->height;
	SDL_Rect d;
	d.w = (int)(f->width * tl); d.h = (int)(f->height * tl);
	d.x = (ow - d.w) / 2; d.y = (oh - d.h) / 2;
	SDL_RenderClear(g_vd.ren);
	if(SDL_RenderCopy(g_vd.ren, g_vd.tex, NULL, &d) != 0 && ++g_vd.lan_loi_lien >= 30)
	{
		fprintf(stderr, "VIDEO|loi|ve hinh loi lien tiep: %s\n", SDL_GetError());
		fflush(stderr);
		video_tat();
		return;
	}
	SDL_RenderPresent(g_vd.ren);
	g_vd.so_ve = so;
}
static bool g_vd_dang_bat(void) { return g_vd.ren != NULL; }
#elif defined(__APPLE__) && defined(CAUNOI_VIDEO)
// ---------------------------------------------------------------- 21/10: CUA SO HINH MUOT (macOS)
// Cua so SDL2 (Metal) KHONG VIEN, muc "noi" (tren cua so app), BO QUA CHUOT (chuot roi xuong o xem cua app),
// khong hien icon Dock / khong gianh ban phim. App gui "M x y w h" moi khi o xem doi cho / doi co.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dlfcn.h>
// 21/10: nap SDL2 LUC CAN (dlopen) - khong nap AppKit luc khoi dong (lenh tim / dang_ky / chay nen khong lien quan)
static __typeof__(SDL_SetHint) *p_SDL_SetHint;
static __typeof__(SDL_GetWindowSize) *p_SDL_GetWindowSize;
static __typeof__(SDL_GetWindowPosition) *p_SDL_GetWindowPosition;
static __typeof__(SDL_Init) *p_SDL_Init;
static __typeof__(SDL_GetError) *p_SDL_GetError;
static __typeof__(SDL_CreateWindow) *p_SDL_CreateWindow;
static __typeof__(SDL_GetWindowWMInfo) *p_SDL_GetWindowWMInfo;
static __typeof__(SDL_CreateRenderer) *p_SDL_CreateRenderer;
static __typeof__(SDL_GetRendererInfo) *p_SDL_GetRendererInfo;
static __typeof__(SDL_SetYUVConversionMode) *p_SDL_SetYUVConversionMode;
static __typeof__(SDL_SetRenderDrawColor) *p_SDL_SetRenderDrawColor;
static __typeof__(SDL_RenderClear) *p_SDL_RenderClear;
static __typeof__(SDL_RenderPresent) *p_SDL_RenderPresent;
static __typeof__(SDL_DestroyTexture) *p_SDL_DestroyTexture;
static __typeof__(SDL_DestroyRenderer) *p_SDL_DestroyRenderer;
static __typeof__(SDL_DestroyWindow) *p_SDL_DestroyWindow;
static __typeof__(SDL_SetWindowPosition) *p_SDL_SetWindowPosition;
static __typeof__(SDL_SetWindowSize) *p_SDL_SetWindowSize;
static __typeof__(SDL_PumpEvents) *p_SDL_PumpEvents;
static __typeof__(SDL_FlushEvents) *p_SDL_FlushEvents;
static __typeof__(SDL_CreateTexture) *p_SDL_CreateTexture;
static __typeof__(SDL_UpdateNVTexture) *p_SDL_UpdateNVTexture;
static __typeof__(SDL_UpdateYUVTexture) *p_SDL_UpdateYUVTexture;
static __typeof__(SDL_GetRendererOutputSize) *p_SDL_GetRendererOutputSize;
static __typeof__(SDL_RenderCopy) *p_SDL_RenderCopy;
static int sdl_nap(void)
{
	static int da = 0;       // 0 chua thu, 1 ok, -1 loi
	if(da) return da > 0;
	da = -1;
	void *h = dlopen("@executable_path/libs/libSDL2-2.0.0.dylib", RTLD_NOW | RTLD_LOCAL);
	if(!h) h = dlopen("libSDL2-2.0.0.dylib", RTLD_NOW | RTLD_LOCAL);
	if(!h)
	{
		fprintf(stderr, "VIDEO|loi|khong nap duoc SDL2: %s\n", dlerror());
		fflush(stderr);
		return 0;
	}
	if(0
		|| !(p_SDL_SetHint = (__typeof__(SDL_SetHint) *)dlsym(h, "SDL_SetHint"))
		|| !(p_SDL_GetWindowSize = (__typeof__(SDL_GetWindowSize) *)dlsym(h, "SDL_GetWindowSize"))
		|| !(p_SDL_GetWindowPosition = (__typeof__(SDL_GetWindowPosition) *)dlsym(h, "SDL_GetWindowPosition"))
		|| !(p_SDL_Init = (__typeof__(SDL_Init) *)dlsym(h, "SDL_Init"))
		|| !(p_SDL_GetError = (__typeof__(SDL_GetError) *)dlsym(h, "SDL_GetError"))
		|| !(p_SDL_CreateWindow = (__typeof__(SDL_CreateWindow) *)dlsym(h, "SDL_CreateWindow"))
		|| !(p_SDL_GetWindowWMInfo = (__typeof__(SDL_GetWindowWMInfo) *)dlsym(h, "SDL_GetWindowWMInfo"))
		|| !(p_SDL_CreateRenderer = (__typeof__(SDL_CreateRenderer) *)dlsym(h, "SDL_CreateRenderer"))
		|| !(p_SDL_GetRendererInfo = (__typeof__(SDL_GetRendererInfo) *)dlsym(h, "SDL_GetRendererInfo"))
		|| !(p_SDL_SetYUVConversionMode = (__typeof__(SDL_SetYUVConversionMode) *)dlsym(h, "SDL_SetYUVConversionMode"))
		|| !(p_SDL_SetRenderDrawColor = (__typeof__(SDL_SetRenderDrawColor) *)dlsym(h, "SDL_SetRenderDrawColor"))
		|| !(p_SDL_RenderClear = (__typeof__(SDL_RenderClear) *)dlsym(h, "SDL_RenderClear"))
		|| !(p_SDL_RenderPresent = (__typeof__(SDL_RenderPresent) *)dlsym(h, "SDL_RenderPresent"))
		|| !(p_SDL_DestroyTexture = (__typeof__(SDL_DestroyTexture) *)dlsym(h, "SDL_DestroyTexture"))
		|| !(p_SDL_DestroyRenderer = (__typeof__(SDL_DestroyRenderer) *)dlsym(h, "SDL_DestroyRenderer"))
		|| !(p_SDL_DestroyWindow = (__typeof__(SDL_DestroyWindow) *)dlsym(h, "SDL_DestroyWindow"))
		|| !(p_SDL_SetWindowPosition = (__typeof__(SDL_SetWindowPosition) *)dlsym(h, "SDL_SetWindowPosition"))
		|| !(p_SDL_SetWindowSize = (__typeof__(SDL_SetWindowSize) *)dlsym(h, "SDL_SetWindowSize"))
		|| !(p_SDL_PumpEvents = (__typeof__(SDL_PumpEvents) *)dlsym(h, "SDL_PumpEvents"))
		|| !(p_SDL_FlushEvents = (__typeof__(SDL_FlushEvents) *)dlsym(h, "SDL_FlushEvents"))
		|| !(p_SDL_CreateTexture = (__typeof__(SDL_CreateTexture) *)dlsym(h, "SDL_CreateTexture"))
		|| !(p_SDL_UpdateNVTexture = (__typeof__(SDL_UpdateNVTexture) *)dlsym(h, "SDL_UpdateNVTexture"))
		|| !(p_SDL_UpdateYUVTexture = (__typeof__(SDL_UpdateYUVTexture) *)dlsym(h, "SDL_UpdateYUVTexture"))
		|| !(p_SDL_GetRendererOutputSize = (__typeof__(SDL_GetRendererOutputSize) *)dlsym(h, "SDL_GetRendererOutputSize"))
		|| !(p_SDL_RenderCopy = (__typeof__(SDL_RenderCopy) *)dlsym(h, "SDL_RenderCopy")))
	{
		fprintf(stderr, "VIDEO|loi|SDL2 thieu ham\n");
		fflush(stderr);
		return 0;
	}
	da = 1;
	return 1;
}
#define SDL_SetHint p_SDL_SetHint
#define SDL_GetWindowSize p_SDL_GetWindowSize
#define SDL_GetWindowPosition p_SDL_GetWindowPosition
#define SDL_Init p_SDL_Init
#define SDL_GetError p_SDL_GetError
#define SDL_CreateWindow p_SDL_CreateWindow
#define SDL_GetWindowWMInfo p_SDL_GetWindowWMInfo
#define SDL_CreateRenderer p_SDL_CreateRenderer
#define SDL_GetRendererInfo p_SDL_GetRendererInfo
#define SDL_SetYUVConversionMode p_SDL_SetYUVConversionMode
#define SDL_SetRenderDrawColor p_SDL_SetRenderDrawColor
#define SDL_RenderClear p_SDL_RenderClear
#define SDL_RenderPresent p_SDL_RenderPresent
#define SDL_DestroyTexture p_SDL_DestroyTexture
#define SDL_DestroyRenderer p_SDL_DestroyRenderer
#define SDL_DestroyWindow p_SDL_DestroyWindow
#define SDL_SetWindowPosition p_SDL_SetWindowPosition
#define SDL_SetWindowSize p_SDL_SetWindowSize
#define SDL_PumpEvents p_SDL_PumpEvents
#define SDL_FlushEvents p_SDL_FlushEvents
#define SDL_CreateTexture p_SDL_CreateTexture
#define SDL_UpdateNVTexture p_SDL_UpdateNVTexture
#define SDL_UpdateYUVTexture p_SDL_UpdateYUVTexture
#define SDL_GetRendererOutputSize p_SDL_GetRendererOutputSize
#define SDL_RenderCopy p_SDL_RenderCopy
typedef struct
{
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Texture *tex;
	int tex_w, tex_h, tex_nv12;
	void *ns;               // NSWindow*
	bool an;
	uint32_t so_ve;
	int lan_loi_lien;
} Video;
static Video g_vd;
static bool g_sdl_ok = false;
static unsigned g_vd_dem_ve = 0;

static void ns_bool(void *o, const char *sel, bool v)
{
	if(o) ((void (*)(id, SEL, BOOL))objc_msgSend)((id)o, sel_registerName(sel), v ? YES : NO);
}
static void ns_long(void *o, const char *sel, long v)
{
	if(o) ((void (*)(id, SEL, long))objc_msgSend)((id)o, sel_registerName(sel), v);
}
static void ns_obj(void *o, const char *sel, void *v)
{
	if(o) ((void (*)(id, SEL, id))objc_msgSend)((id)o, sel_registerName(sel), (id)v);
}

static void video_tat(void)
{
	if(g_vd.tex) SDL_DestroyTexture(g_vd.tex);
	if(g_vd.ren) SDL_DestroyRenderer(g_vd.ren);
	if(g_vd.win) SDL_DestroyWindow(g_vd.win);
	memset(&g_vd, 0, sizeof(g_vd));
}

// Core Animation chi dua hinh len man hinh khi "giao dich" duoc gui di (binh thuong vong su kien cua app lam viec nay).
// Cau noi la chuong trinh dong lenh, khong co vong su kien -> hinh ve xong van nam trong bo dem, man hinh van den.
static void ca_day_len(void)
{
	static Class ca = NULL;
	if(!ca) ca = objc_getClass("CATransaction");
	if(ca) ((void (*)(id, SEL))objc_msgSend)((id)ca, sel_registerName("flush"));
}

static void video_hien(bool hien)
{
	if(!g_vd.ns) return;
	if(hien) ((void (*)(id, SEL))objc_msgSend)((id)g_vd.ns, sel_registerName("orderFrontRegardless"));
	else ns_obj(g_vd.ns, "orderOut:", NULL);
}

static void video_bat_mac(int x, int y, int w, int h)
{
	video_tat();
	if(!sdl_nap())
		return;
	if(!g_sdl_ok)
	{
		SDL_SetHint("SDL_MAC_BACKGROUND_APP", "1");                 // khong icon Dock, khong cuop ban phim
		SDL_SetHint("SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN", "1");
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
		SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
		if(SDL_Init(SDL_INIT_VIDEO) != 0)
		{
			fprintf(stderr, "VIDEO|loi|SDL_Init: %s\n", SDL_GetError());
			fflush(stderr);
			return;
		}
		g_sdl_ok = true;
	}
	g_vd.win = SDL_CreateWindow("cau-noi", x, y, w > 16 ? w : 16, h > 16 ? h : 16,
			SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR);
	if(!g_vd.win)
	{
		fprintf(stderr, "VIDEO|loi|SDL_CreateWindow: %s\n", SDL_GetError());
		fflush(stderr);
		return;
	}
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if(SDL_GetWindowWMInfo(g_vd.win, &wm) && wm.subsystem == SDL_SYSWM_COCOA)
	{
		g_vd.ns = (void *)wm.info.cocoa.window;
		ns_bool(g_vd.ns, "setIgnoresMouseEvents:", true);     // chuot xuyen qua -> o xem cua app
		ns_bool(g_vd.ns, "setHasShadow:", false);
		ns_long(g_vd.ns, "setLevel:", 3);                       // NSFloatingWindowLevel: tren cua so app
		ns_long(g_vd.ns, "setCollectionBehavior:", (1 << 0) | (1 << 4));  // moi Space + khong vao Mission Control
	}
	g_vd.ren = SDL_CreateRenderer(g_vd.win, -1, SDL_RENDERER_ACCELERATED);
	if(!g_vd.ren)
		g_vd.ren = SDL_CreateRenderer(g_vd.win, -1, 0);
	if(!g_vd.ren)
	{
		fprintf(stderr, "VIDEO|loi|SDL_CreateRenderer: %s\n", SDL_GetError());
		fflush(stderr);
		video_tat();
		return;
	}
	SDL_RendererInfo ri;
	SDL_GetRendererInfo(g_vd.ren, &ri);
	SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_BT709);
	SDL_SetRenderDrawColor(g_vd.ren, 0, 0, 0, 255);
	SDL_RenderClear(g_vd.ren);
	SDL_RenderPresent(g_vd.ren);
	video_hien(true);
	ca_day_len();
	fprintf(stderr, "VIDEO|bat|%s\n", ri.name ? ri.name : "?");
	fflush(stderr);
}

static void video_nhip(AVFrame *f, uint32_t so)
{
	video_hwnd_moi = 0;
	bool doi = false;
	if(video_m_moi)
	{
		video_m_moi = 0;
		int x = video_mx, y = video_my, w = video_mw, h = video_mh;
		fprintf(stderr, "VIDEO|M|%d,%d %dx%d\n", x, y, w, h);
		fflush(stderr);
		if(!g_vd.ren)
			video_bat_mac(x, y, w, h);
		else
		{
			SDL_SetWindowPosition(g_vd.win, x, y);
			SDL_SetWindowSize(g_vd.win, w > 1 ? w : 1, h > 1 ? h : 1);
		}
		doi = true;
	}
	int l = video_lenh;
	if(l)
	{
		video_lenh = 0;
		if(l == 1) { video_tat(); fprintf(stderr, "VIDEO|tat\n"); fflush(stderr); }
		else if(g_vd.ren) { g_vd.an = (l == 2); video_hien(!g_vd.an); g_vd.so_ve = 0;
			fprintf(stderr, "VIDEO|%s|da ve %u\n", g_vd.an ? "an" : "hien", g_vd_dem_ve); fflush(stderr); }
	}
	if(!g_vd.ren)
		return;
	SDL_PumpEvents();
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	if(g_vd.an || !f || !dinh_dang_ho_tro(f) || (so == g_vd.so_ve && !doi))
		return;
	int nv12 = f->format == AV_PIX_FMT_NV12;
	if(!g_vd.tex || g_vd.tex_w != f->width || g_vd.tex_h != f->height || g_vd.tex_nv12 != nv12)
	{
		if(g_vd.tex) SDL_DestroyTexture(g_vd.tex);
		g_vd.tex = SDL_CreateTexture(g_vd.ren, nv12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV,
				SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
		g_vd.tex_w = f->width; g_vd.tex_h = f->height; g_vd.tex_nv12 = nv12;
		if(!g_vd.tex)
			return;
	}
	int kq = nv12
		? SDL_UpdateNVTexture(g_vd.tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1])
		: SDL_UpdateYUVTexture(g_vd.tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1],
				f->data[2], f->linesize[2]);
	if(kq != 0)
	{
		if(++g_vd.lan_loi_lien >= 30)
		{
			fprintf(stderr, "VIDEO|loi|ve hinh loi lien tiep: %s\n", SDL_GetError());
			fflush(stderr);
			video_tat();
		}
		return;
	}
	g_vd.lan_loi_lien = 0;
	int ow = 0, oh = 0;
	SDL_GetRendererOutputSize(g_vd.ren, &ow, &oh);
	double tl = (double)ow / f->width < (double)oh / f->height ? (double)ow / f->width : (double)oh / f->height;
	SDL_Rect d;
	d.w = (int)(f->width * tl); d.h = (int)(f->height * tl);
	d.x = (ow - d.w) / 2; d.y = (oh - d.h) / 2;
	SDL_RenderClear(g_vd.ren);
	if(SDL_RenderCopy(g_vd.ren, g_vd.tex, NULL, &d) != 0 && ++g_vd.lan_loi_lien >= 30)
	{
		fprintf(stderr, "VIDEO|loi|ve hinh loi lien tiep: %s\n", SDL_GetError());
		fflush(stderr);
		video_tat();
		return;
	}
	SDL_RenderPresent(g_vd.ren);
	ca_day_len();          // 21/10: cau noi khong chay vong su kien cua macOS -> phai tu day khung len man hinh
	g_vd.so_ve = so;
	g_vd_dem_ve++;
	if(g_vd_dem_ve == 1)
	{
		int wx = 0, wy = 0, ww = 0, wh = 0;
		SDL_GetWindowPosition(g_vd.win, &wx, &wy);
		SDL_GetWindowSize(g_vd.win, &ww, &wh);
		fprintf(stderr, "VIDEO|ve_dau|cua so %d,%d %dx%d diem, ve %dx%d diem anh, hien=%d\n", wx, wy, ww, wh, ow, oh,
			g_vd.ns ? (int)((BOOL (*)(id, SEL))objc_msgSend)((id)g_vd.ns, sel_registerName("isVisible")) : -1);
		fflush(stderr);
	}
}
static bool g_vd_dang_bat(void) { return g_vd.ren != NULL; }
#else
static void video_nhip(AVFrame *f, uint32_t so) { (void)f; (void)so; video_hwnd_moi = 0; video_lenh = 0; video_m_moi = 0; }
static void video_tat(void) {}
static bool g_vd_dang_bat(void) { return false; }
#endif

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
		else if(dong[0] == 'V')
		{
			long long h = atoll(dong + 1);
			if(h > 0)
				video_hwnd_moi = h;
		}
		else if(dong[0] == 'M')
		{
			int x = 0, y = 0, w = 0, h = 0;
			if(sscanf(dong + 1, "%d %d %d %d", &x, &y, &w, &h) == 4 && w > 0 && h > 0)
			{
				video_mx = x; video_my = y; video_mw = w; video_mh = h;
				video_m_moi = 1;
			}
		}
		else if(dong[0] == 'v')
			video_lenh = 1;
		else if(dong[0] == 'a')
			video_lenh = 2;
		else if(dong[0] == 'A')
			video_lenh = 3;
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
		uint64_t t_dem = chiaki_time_now_monotonic_ms();
		unsigned dem_cu = g_dem_giai_ma, gui_cu = 0, gui_dem = 0;
		while(!phat_dung && !ctx.da_thoat)
		{
			uint64_t t_bay_gio = chiaki_time_now_monotonic_ms();
			if(t_bay_gio - t_dem >= 10000)     // 17/10: moi 10 giay ghi so hinh/giay PS5 gui ve + so hinh app lay
			{
				double giay = (t_bay_gio - t_dem) / 1000.0;
				if(ctx.da_ket_noi)
					fprintf(stderr, "[I] HINH PS5: %.1f hinh/giay gui ve, app lay %.1f hinh/giay (xin %d)\n",
						(g_dem_giai_ma - dem_cu) / giay, (gui_dem - gui_cu) / giay, g_fps);
				t_dem = t_bay_gio;
				dem_cu = g_dem_giai_ma;
				gui_cu = gui_dem;
			}
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
			video_nhip(hinh_moi_nhat, so_hinh);         // 20/10: ve cua so hinh muot (neu app bat)
			while(phat_yeu_cau > 0 || phat_yeu_cau_nho > 0)
			{
				if(phat_yeu_cau > 0)
				{
					phat_yeu_cau--;
					gui_hinh(hinh_moi_nhat, so_hinh, &bgr, &bgr_cap);
					gui_dem++;
				}
				else
				{
					phat_yeu_cau_nho--;
					gui_hinh_nho(hinh_moi_nhat, so_hinh, &bgr, &bgr_cap, &nho, &nho_cap);
					gui_dem++;
				}
			}
			ngu_ms(g_vd_dang_bat() ? 2 : 5);
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
			video_nhip(NULL, so_hinh);                  // 20/10: van xu ly cua so hinh (doi co, an/hien) luc cho
			ngu_ms(20);
		}
	}

	video_tat();
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
#if defined(_WIN32) && defined(CAUNOI_VIDEO)
				"      cua so muot: stdin V <hwnd> = ve hinh vao o cua app, v = tat, a = an, A = hien\n"
#endif
#if defined(__APPLE__) && defined(CAUNOI_VIDEO)
				"      cua so muot (mac): stdin M x y w h = cua so hinh dat dung o xem, v = tat, a = an, A = hien\n"
#endif
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
