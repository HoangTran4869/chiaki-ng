// Cau noi chiaki-ng cho ung dung iPhone "Dich Phu De Game" - AGPL-3.0
#include "cau_noi_ios.h"

#include <chiaki/common.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>
#include <chiaki/discovery.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ChiakiLog g_log;
static cn_ham_nhat_ky g_nhat_ky;
static void *g_nhat_ky_user;
static bool g_da_khoi_dong;

static void log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)user;
	if(g_nhat_ky)
		g_nhat_ky((int)level, msg, g_nhat_ky_user);
}

int cn_khoi_dong(cn_ham_nhat_ky nhat_ky, void *user)
{
	g_nhat_ky = nhat_ky;
	g_nhat_ky_user = user;
	chiaki_log_init(&g_log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, log_cb, NULL);
	// 27/10: iPhone mac dinh GIET ung dung khi ghi vao ket noi PS5 da dong (SIGPIPE) -> bo qua,
	// de loi tra ve binh thuong (PS5 tu choi ghep doi / ngat phien thi khong van ung dung)
	signal(SIGPIPE, SIG_IGN);
	if(g_da_khoi_dong)
		return 0;
	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(&g_log, "chiaki_lib_init loi: %s", chiaki_error_string(err));
		return 1;
	}
	g_da_khoi_dong = true;
	return 0;
}

static int hex_sang_byte(const char *hex, uint8_t *out, size_t out_size)
{
	if(!hex || strlen(hex) != out_size * 2)
		return 1;
	for(size_t i = 0; i < out_size; i++)
	{
		unsigned v;
		if(sscanf(hex + i * 2, "%2x", &v) != 1)
			return 1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static void byte_sang_hex(const uint8_t *b, size_t n, char *out)
{
	for(size_t i = 0; i < n; i++)
		sprintf(out + i * 2, "%02x", b[i]);
	out[n * 2] = 0;
}

// ---------------------------------------------------------------- ghep doi

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

int cn_ghep_doi(const char *ip, const uint8_t tai_khoan[8], uint32_t ma_pin, bool ps4, cn_ket_qua_ghep *kq)
{
	memset(kq, 0, sizeof(*kq));
	ChiakiRegistInfo info;
	memset(&info, 0, sizeof(info));
	info.target = ps4 ? CHIAKI_TARGET_PS4_10 : CHIAKI_TARGET_PS5_1;
	info.host = ip;
	info.broadcast = false;
	info.psn_online_id = NULL;
	memcpy(info.psn_account_id, tai_khoan, CHIAKI_PSN_ACCOUNT_ID_SIZE);
	info.pin = ma_pin;
	info.console_pin = 0;
	info.holepunch_info = NULL;
	info.rudp = NULL;

	DangKy dk;
	memset(&dk, 0, sizeof(dk));
	chiaki_mutex_init(&dk.mutex, false);
	chiaki_cond_init(&dk.cond);
	ChiakiRegist regist;
	if(chiaki_regist_start(&regist, &g_log, &info, dang_ky_cb, &dk) != CHIAKI_ERR_SUCCESS)
	{
		snprintf(kq->loi, sizeof(kq->loi), "Không bắt đầu ghép đôi được");
		chiaki_cond_fini(&dk.cond);
		chiaki_mutex_fini(&dk.mutex);
		return 2;
	}
	chiaki_mutex_lock(&dk.mutex);
	for(int i = 0; i < 300 && !dk.xong; i++)   // toi da ~30 giay
		chiaki_cond_timedwait(&dk.cond, &dk.mutex, 100);
	bool xong = dk.xong;
	chiaki_mutex_unlock(&dk.mutex);
	if(!xong)
		chiaki_regist_stop(&regist);
	chiaki_regist_fini(&regist);
	chiaki_cond_fini(&dk.cond);
	chiaki_mutex_fini(&dk.mutex);

	if(xong && dk.kieu == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS)
	{
		kq->ok = true;
		byte_sang_hex((const uint8_t *)dk.may.rp_regist_key, sizeof(dk.may.rp_regist_key), kq->regist_key);
		byte_sang_hex(dk.may.rp_key, sizeof(dk.may.rp_key), kq->morning);
		dk.may.server_nickname[sizeof(dk.may.server_nickname) - 1] = 0;
		snprintf(kq->ten_may, sizeof(kq->ten_may), "%s", dk.may.server_nickname);
		return 0;
	}
	snprintf(kq->loi, sizeof(kq->loi), "%s", !xong ? "Hết giờ - PS5 không trả lời" :
		(dk.kieu == CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED ? "Đã huỷ" : "PS5 từ chối - kiểm tra mã 8 số và tài khoản"));
	return 3;
}

// ---------------------------------------------------------------- phien choi tu xa

typedef struct
{
	ChiakiSession session;
	ChiakiConnectInfo ci;
	char ip[64];
	cn_ham_hinh hinh;
	cn_ham_su_kien su_kien;
	void *user;
} Phien;

static bool hinh_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	(void)frame_recovered;
	Phien *p = user;
	if(p->hinh)
		p->hinh(buf, buf_size, frames_lost, p->user);
	return true;
}

static void su_kien_cb(ChiakiEvent *event, void *user)
{
	Phien *p = user;
	if(!p->su_kien)
		return;
	char ly_do[200];
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			p->su_kien(CN_SK_DA_KET_NOI, "", p->user);
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			p->su_kien(CN_SK_CAN_PIN, "", p->user);
			break;
		case CHIAKI_EVENT_QUIT:
			snprintf(ly_do, sizeof(ly_do), "%s%s%s",
					chiaki_quit_reason_string(event->quit.reason),
					event->quit.reason_str ? " - " : "",
					event->quit.reason_str ? event->quit.reason_str : "");
			p->su_kien(CN_SK_KET_THUC, ly_do, p->user);
			break;
		default:
			break;
	}
}

static void khong_hien_cb(void *user, bool khong_hien)
{
	Phien *p = user;
	if(p->su_kien)
		p->su_kien(khong_hien ? CN_SK_BI_CHAN_HINH : CN_SK_HET_CHAN_HINH, "", p->user);
}

void *cn_ket_noi(const char *ip, const char *regist_key_hex, const char *morning_hex, bool ps4, int fps,
		cn_ham_hinh hinh, cn_ham_su_kien su_kien, void *user, char *loi, size_t loi_size)
{
	Phien *p = calloc(1, sizeof(Phien));
	if(!p)
		return NULL;
	snprintf(p->ip, sizeof(p->ip), "%s", ip);
	p->hinh = hinh;
	p->su_kien = su_kien;
	p->user = user;

	ChiakiConnectInfo *ci = &p->ci;
	ci->ps5 = !ps4;
	ci->host = p->ip;
	if(hex_sang_byte(regist_key_hex, (uint8_t *)ci->regist_key, sizeof(ci->regist_key)) != 0
		|| hex_sang_byte(morning_hex, ci->morning, sizeof(ci->morning)) != 0)
	{
		snprintf(loi, loi_size, "Thông tin ghép đôi bị hỏng - ghép đôi lại");
		free(p);
		return NULL;
	}
	chiaki_connect_video_profile_preset(&ci->video_profile, CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
		fps >= 60 ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30);
	if(fps >= 60)
		ci->video_profile.bitrate = 30000;
	ci->video_profile.codec = CHIAKI_CODEC_H264;
	ci->video_profile_auto_downgrade = true;
	ci->enable_keyboard = false;
	ci->enable_dualsense = false;
	ci->audio_video_disabled = CHIAKI_AUDIO_DISABLED;
	ci->auto_regist = false;
	ci->holepunch_session = NULL;
	ci->packet_loss_max = 0.05;
	ci->enable_idr_on_fec_failure = true;

	ChiakiErrorCode err = chiaki_session_init(&p->session, ci, &g_log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		snprintf(loi, loi_size, "Không khởi tạo được phiên: %s", chiaki_error_string(err));
		free(p);
		return NULL;
	}
	ChiakiCtrlDisplaySink sink;
	memset(&sink, 0, sizeof(sink));
	sink.user = p;
	sink.cantdisplay_cb = khong_hien_cb;
	chiaki_session_ctrl_set_display_sink(&p->session, &sink);
	chiaki_session_set_event_cb(&p->session, su_kien_cb, p);
	chiaki_session_set_video_sample_cb(&p->session, hinh_cb, p);

	err = chiaki_session_start(&p->session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		snprintf(loi, loi_size, "Không kết nối được: %s", chiaki_error_string(err));
		chiaki_session_fini(&p->session);
		free(p);
		return NULL;
	}
	return p;
}

void cn_ngat(void *phien)
{
	Phien *p = phien;
	if(!p)
		return;
	chiaki_session_stop(&p->session);
	chiaki_session_join(&p->session);
	chiaki_session_fini(&p->session);
	free(p);
}

// ---------------------------------------------------------------- tim PS5 (hoi tung dia chi)

typedef struct
{
	cn_ham_thay_may thay;
	void *user;
} TimCtx;

static void tim_cb(ChiakiDiscoveryHost *host, void *user)
{
	TimCtx *t = user;
	if(!t->thay || !host->host_addr)
		return;
	t->thay(host->host_addr, host->host_name ? host->host_name : "",
		host->host_type ? host->host_type : "",
		chiaki_discovery_host_state_string(host->state), t->user);
}

static bool la_mang_noi_bo(uint32_t ip)   // ip theo thu tu may (host order)
{
	return (ip >> 24) == 10 || (ip >> 20) == (172 << 4 | 1) || (ip >> 16) == (192 << 8 | 168);
}

int cn_tim_ps5(int mili_giay, cn_ham_thay_may thay, void *user)
{
	// cac mang /24 cua may (Wi-Fi, day mang...) - toi da 4 mang
	uint32_t mang[4];
	uint32_t cua_minh[4];
	int so_mang = 0;
	struct ifaddrs *ds = NULL;
	if(getifaddrs(&ds) == 0)
	{
		for(struct ifaddrs *i = ds; i && so_mang < 4; i = i->ifa_next)
		{
			if(!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
				continue;
			uint32_t ip = ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
			if(!la_mang_noi_bo(ip))
				continue;
			uint32_t m = ip & 0xFFFFFF00u;
			bool trung = false;
			for(int k = 0; k < so_mang; k++)
				if(mang[k] == m)
					trung = true;
			if(trung)
				continue;
			char s[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, s, sizeof(s));
			CHIAKI_LOGI(&g_log, "Tim PS5 trong mang %s (%s)", s, i->ifa_name);
			cua_minh[so_mang] = ip;
			mang[so_mang++] = m;
		}
		freeifaddrs(ds);
	}
	if(so_mang == 0)
	{
		CHIAKI_LOGE(&g_log, "Khong thay mang Wi-Fi noi bo");
		return 1;
	}

	ChiakiDiscovery discovery;
	if(chiaki_discovery_init(&discovery, &g_log, AF_INET) != CHIAKI_ERR_SUCCESS)
		return 2;
	TimCtx ctx = { thay, user };
	ChiakiDiscoveryThread thread;
	if(chiaki_discovery_thread_start(&thread, &discovery, tim_cb, &ctx) != CHIAKI_ERR_SUCCESS)
	{
		chiaki_discovery_fini(&discovery);
		return 2;
	}

	ChiakiDiscoveryPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);

	int so_vong = mili_giay >= 1500 ? 2 : 1;      // hoi 2 vong cho chac (goi UDP co the rot)
	for(int vong = 0; vong < so_vong; vong++)
	{
		for(int k = 0; k < so_mang; k++)
			for(uint32_t d = 1; d < 255; d++)
			{
				uint32_t ip = mang[k] | d;
				if(ip == cua_minh[k])
					continue;
				addr.sin_addr.s_addr = htonl(ip);
				chiaki_discovery_send(&discovery, &packet, (struct sockaddr *)&addr, sizeof(addr));
				if((d & 31) == 0)
					usleep(2000);     // gian ra chut cho bo dinh tuyen khong rot goi
			}
		usleep((useconds_t)(mili_giay / so_vong) * 1000);
	}

	chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);
	return 0;
}
