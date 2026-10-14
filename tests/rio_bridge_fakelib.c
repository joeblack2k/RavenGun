/* Nep-librio.so voor tests/rio_bridge_test.sh. Twee smaken, gekozen met -DRG_GOEDE_BUILD:
 *   goed:  de vier functies beginnen met de handtekening uit de testtabel (de eerste 8 bytes van de echte librio.so van NERF)
 *          en daarna int3. De brug overschrijft het begin met een sprong; een functie die NIET is aangepast, crasht op int3.
 *   fout:  andere bytes ("een andere build"): geldige code die een vaste waarde teruggeeft. De brug moet die kopie met rust laten.
 */
#ifdef RG_GOEDE_BUILD
__asm__(".text\n"
        ".globl RIO_Connected\n.type RIO_Connected,@function\nRIO_Connected:\n .byte 0x48,0x8b,0x05,0x79,0xfc,0x21,0x00,0x83\n .fill 24,1,0xcc\n"
        ".globl RIO_SW_Count\n.type RIO_SW_Count,@function\nRIO_SW_Count:\n .byte 0x8d,0x47,0xdc,0x83,0xf8,0x27,0x77,0x38\n .fill 24,1,0xcc\n"
        ".globl RIO_ADC_Val\n.type RIO_ADC_Val,@function\nRIO_ADC_Val:\n .byte 0x89,0xff,0x48,0x8d,0x04,0xbf,0x48,0x8d\n .fill 24,1,0xcc\n"
        ".globl RIO_ADC_Val_Ex\n.type RIO_ADC_Val_Ex,@function\nRIO_ADC_Val_Ex:\n .byte 0x8d,0x47,0xed,0x83,0xf8,0x03,0x77,0x40\n .fill 24,1,0xcc\n");
#else
int RIO_Connected(void) { return 0x3333; }
unsigned RIO_SW_Count(int id) { (void)id; return 0x4444; }
int RIO_ADC_Val(int ch) { (void)ch; return 0x5555; }
int RIO_ADC_Val_Ex(int ch, int *d) { (void)ch; if (d) *d = 0; return 0x6666; }
#endif
