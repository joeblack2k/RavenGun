// The logic of rg-mfcam.dll's own ShareK object (tools/rg-mfcam/sharek_fake.h): which class we answer for, which interfaces,
// and that the slot table matches the call sites measured in MK_AGP3_FINAL.exe (objdump, 0x47c000..0x483400).
#include <cstdio>
#include <set>

#include "../tools/rg-mfcam/sharek_fake.h"

using namespace rg_sharek;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

int main() {
    // The GUIDs as in the exe (0x9b2bbc = CLSID, 0x9b2bac = IID; in-memory layout little-endian, 16 bytes).
    const uint8_t clsid_bytes[16] = {0x2a, 0x4b, 0xc4, 0xcc, 0x9a, 0x70, 0xa8, 0x33, 0xa1, 0xb0, 0x5d, 0xbe, 0x25, 0xb1, 0x9c, 0x2c};
    const uint8_t iid_bytes[16] = {0x7f, 0x6f, 0x11, 0xc2, 0x46, 0xcf, 0x68, 0x3c, 0xb1, 0x87, 0x9d, 0x0f, 0xe9, 0xf7, 0x0e, 0xd0};
    static_assert(sizeof(Guid) == 16, "GUID layout");
    CHECK(std::memcmp(&kClsid, clsid_bytes, 16) == 0);
    CHECK(std::memcmp(&kIid, iid_bytes, 16) == 0);

    // Only the ShareK class is ours; AMAuthd's IAuth (4603BB03-058D-43D9-B96F-639BE908C1ED) goes to the real CoCreateInstance.
    const Guid iauth = {0x4603bb03u, 0x058d, 0x43d9, {0xb9, 0x6f, 0x63, 0x9b, 0xe9, 0x08, 0xc1, 0xed}};
    CHECK(wants_clsid(kClsid));
    CHECK(!wants_clsid(iauth));
    CHECK(!wants_clsid(kIid));
    CHECK(!wants_clsid(kIidUnknown));

    CHECK(knows_iid(kIid));
    CHECK(knows_iid(kIidUnknown));
    CHECK(knows_iid(kIidDispatch));
    const Guid iauth_iid = {0x045a5150u, 0xd2b3, 0x4590, {0xa3, 0x8b, 0xc1, 0x15, 0x86, 0x78, 0xe1, 0xac}};
    CHECK(!knows_iid(iauth_iid));

    // Each slot occurs at most once, lies within the vtable and after the IDispatch slots (0..6), and has 8 or 20 bytes of arguments.
    std::set<int> seen;
    for (int i = 0; i < kSlotCount; ++i) {
        const Slot& s = kSlots[i];
        CHECK(seen.insert(s.index).second);
        CHECK(s.index >= 7 && s.index < kVtableSize);
        CHECK(s.arg_bytes == 8 || s.arg_bytes == 20);
        CHECK(find_slot(s.index) == &s);
    }
    CHECK(find_slot(0) == nullptr && find_slot(63) == nullptr && find_slot(59) == nullptr);

    // The six methods with four extra arguments (push edi/edx/ebx/esi + this): vtable offsets 0x38, 0x3c, 0x44, 0x48, 0x4c, 0x50.
    const int five[] = {14, 15, 17, 18, 19, 20};
    int n20 = 0;
    for (const Slot& s : kSlots) n20 += s.arg_bytes == 20;
    CHECK(n20 == 6);
    for (int i : five) CHECK(find_slot(i) && find_slot(i)->arg_bytes == 20);

    // The three replies that do something: StartIpcClient succeeds (WORD != 0), no restart request, and the
    // ShareK status. Since 2026-10-19 that status can be chosen with RG_SHAREK_STATUS; without that
    // variable it is 1 ("配信機能OFF": the game cleans up the distribution once and then carries on
    // with its work), because a standalone cabinet has nothing to distribute but must keep going. With
    // RG_SHAREK_STATUS=2 you get the old value back (waits for the network).
    int16_t v = 123;
    CHECK(find_slot(7)->kind == Kind::Start && out_word(Kind::Start, v) && v != 0);
    CHECK(find_slot(33)->kind == Kind::Restart && out_word(Kind::Restart, v) && v == 0);
    // The choice from the environment is read once (static) and the conversion can be tested separately.
    CHECK(rg_sharek::status_from_text(nullptr) == 1);
    CHECK(rg_sharek::status_from_text("") == 1);
    CHECK(rg_sharek::status_from_text("0") == 0);
    CHECK(rg_sharek::status_from_text("1") == 1);
    CHECK(rg_sharek::status_from_text("2") == 2);
    CHECK(rg_sharek::status_from_text("3") == 1);        // out of range
    CHECK(rg_sharek::status_from_text("-1") == 1);       // negative
    CHECK(rg_sharek::status_from_text("abc") == 1);      // non-numeric
    CHECK(rg_sharek::status_from_text("2x") == 1);       // not everything consumed
    CHECK(rg_sharek::status_from_text("9", 2) == 2);     // custom default
    unsetenv("RG_SHAREK_STATUS");
    CHECK(find_slot(35)->kind == Kind::Status && out_word(Kind::Status, v) && v == 1);
    v = 77;
    CHECK(!out_word(Kind::Nop, v) && v == 77);   // an empty reply leaves the caller's output (already 0) untouched
    int actions = 0;
    for (const Slot& s : kSlots) actions += s.kind != Kind::Nop;
    CHECK(actions == 3);

    // Slot 8 (shut down) and the results the caller first sets to 0 stay Nop.
    for (int i : {8, 9, 10, 11, 27, 31, 41, 43, 55}) CHECK(find_slot(i) && find_slot(i)->kind == Kind::Nop);

    std::printf("sharek_fake: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
