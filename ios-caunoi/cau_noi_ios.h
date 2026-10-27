// Cau noi chiaki-ng cho ung dung iPhone "Dich Phu De Game" - AGPL-3.0
// Dua tren chiaki-ng (https://github.com/streetpea/chiaki-ng). Ma nguon cau noi: thu muc ios-caunoi.
// Header nay KHONG dung kieu cua chiaki -> Swift goi thang duoc.
#ifndef CAU_NOI_IOS_H
#define CAU_NOI_IOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// su kien cua phien choi tu xa
enum
{
	CN_SK_DA_KET_NOI = 1,      // da vao duoc PS5
	CN_SK_KET_THUC = 2,        // phien ket thuc (chi_tiet = ly do)
	CN_SK_BI_CHAN_HINH = 3,    // PS5 dang mo noi dung duoc bao ve -> khong gui hinh
	CN_SK_HET_CHAN_HINH = 4,
	CN_SK_CAN_PIN = 5          // PS5 bat ma PIN dang nhap Choi tu xa (chua ho tro)
};

typedef void (*cn_ham_nhat_ky)(int muc, const char *tin, void *user);
// buf = 1 khung hinh H.264 dang Annex-B (chi dung trong luc goi, phai chep ra neu giu lai)
typedef void (*cn_ham_hinh)(const uint8_t *buf, size_t len, int32_t so_khung_mat, void *user);
typedef void (*cn_ham_su_kien)(int loai, const char *chi_tiet, void *user);

typedef struct
{
	bool ok;
	char regist_key[33];   // hex
	char morning[33];      // hex
	char ten_may[33];
	char loi[96];
} cn_ket_qua_ghep;

// goi 1 lan khi mo ung dung
int cn_khoi_dong(cn_ham_nhat_ky nhat_ky, void *user);

// ghep doi PS5 (chan toi da ~30 giay -> goi o luong phu)
int cn_ghep_doi(const char *ip, const uint8_t tai_khoan[8], uint32_t ma_pin, bool ps4, cn_ket_qua_ghep *kq);

// mo phien choi tu xa. Tra ve con tro phien (NULL = loi, xem loi)
void *cn_ket_noi(const char *ip, const char *regist_key_hex, const char *morning_hex, bool ps4, int fps,
		cn_ham_hinh hinh, cn_ham_su_kien su_kien, void *user, char *loi, size_t loi_size);

// dong phien (chan toi khi xong)
void cn_ngat(void *phien);

// Tim PS5 trong mang nha KHONG can quyen multicast: hoi rieng tung dia chi (x.x.x.1 -> .254)
// cua moi mang IPv4 noi bo cua may. Chan khoang `mili_giay` (vd 2000) -> goi o luong phu.
// Moi may tra loi -> goi `thay` (co the goi nhieu lan cho cung 1 may).
typedef void (*cn_ham_thay_may)(const char *ip, const char *ten, const char *loai, const char *trang_thai, void *user);
int cn_tim_ps5(int mili_giay, cn_ham_thay_may thay, void *user);

#ifdef __cplusplus
}
#endif

#endif
