/***************************************************************************

  Color Graphics Adapter (CGA) section


  Notes on Port 3D8
  (http://www.clipx.net/ng/interrupts_and_ports/ng2d045.php)

    Port 3D8  -  Color/VGA Mode control register

            xx1x xxxx  Attribute bit 7. 0=blink, 1=Intesity
            xxx1 xxxx  640x200 mode
            xxxx 1xxx  Enable video signal
            xxxx x1xx  Select B/W mode
            xxxx xx1x  Select graphics
            xxxx xxx1  80x25 text


    The usage of the above control register for various modes is:
            xx10 1100  40x25 alpha B/W
            xx10 1000  40x25 alpha color
            xx10 1101  80x25 alpha B/W
            xx10 1001  80x25 alpha color
            xxx0 1110  320x200 graph B/W
            xxx0 1010  320x200 graph color
            xxx1 1110  640x200 graph B/W


    PC1512 display notes

    The PC1512 built-in display adaptor is an emulation of IBM's CGA.  Unlike a
    real CGA, it is not built around a real MC6845 controller, and so attempts
    to get custom video modes out of it may not work as expected. Its 640x200
    CGA mode can be set up to be a 16-color mode rather than mono.

    If you program it with BIOS calls, the PC1512 behaves just like a real CGA,
    except:

    - The 'greyscale' text modes (0 and 2) behave just like the 'color'
      ones (1 and 3). On a color monitor both are in color; on a mono
      monitor both are in greyscale.
    - Mode 5 (the 'greyscale' graphics mode) displays in color, using
      an alternative color palette: Cyan, Red and White.
    - The undocumented 160x100x16 "graphics" mode works correctly.

    (source John Elliott http://www.seasip.info/AmstradXT/pc1512disp.html)


  Cursor signal handling:

  The alpha dots signal is set when a character pixel should be set. This signal is
  also set when the cursor should be displayed. The following formula for alpha
  dots is derived from the schematics:
  ALPHA DOTS = ( ( CURSOR DLY ) AND ( CURSOR BLINK ) ) OR ( ( ( NOT AT7 ) OR CURSOR DLY OR -BLINK OR NOT ENABLE BLINK ) AND ( CHG DOTS ) )

  -CURSOR BLINK = VSYNC DLY (LS393) (changes every 8 vsyncs)
  -BLINK = -CURSOR BLINK (LS393) (changes every 16 vsyncs)
  -CURSOR DLY = -CURSOR signal from mc6845 and LS174
  CHG DOTS = character pixel (from character rom)

  For non-blinking modes this formula reduces to:
  ALPHA DOTS = ( ( CURSOR DLY ) AND ( CURSOR BLINK ) ) OR ( CHG DOTS )

  This means the cursor switches on/off state every 8 vsyncs.


  For blinking modes this formula reduces to:
  ALPHA DOTS = ( ( CURSOR DLY ) AND ( CURSOR BLINK ) ) OR ( ( ( NOT AT7 ) OR CURSOR DLY OR -BLINK ) AND ( CHG DOTS ) )

  So, at the cursor location the attribute blinking is ignored and only regular
  cursor blinking takes place (state switches every 8 vsyncs). On non-cursor
  locations with the highest attribute bits set the character will switch
  on/off every 16 vsyncs. In all other cases the character is displayed as
  usual.


TODO:
- Update more drivers in MESS and MAME and unify with src/emu/video/pc_cga.c
- Separate out more cards/implementations

***************************************************************************/

#include "emu.h"
#include "video/mc6845.h"
#include "video/isa_cga.h"

#define VERBOSE_CGA 0       /* CGA (Color Graphics Adapter) */

#define CGA_PALETTE_SETS 83 /* one for colour, one for mono,
                 * 81 for colour composite */

#define CGA_SCREEN_NAME "screen"
#define CGA_MC6845_NAME "mc6845_cga"

#define CGA_LOG(N,M,A) \
	do { \
		if(VERBOSE_CGA>=N) \
		{ \
			if( M ) \
				logerror("%11.6f: %-24s",machine.time().as_double(),(char*)M ); \
			logerror A; \
		} \
	} while (0)

/***************************************************************************

    Static declarations

***************************************************************************/

static INPUT_PORTS_START( cga )
	PORT_START( "cga_config" )
	PORT_CONFNAME( 0x03, 0x00, "CGA character set")
	PORT_CONFSETTING(0x00, DEF_STR( Normal ))
	PORT_CONFSETTING(0x01, "Alternative")
	PORT_CONFNAME( 0x1C, 0x00, "CGA monitor type")
	PORT_CONFSETTING(0x00, "Colour RGB")
	PORT_CONFSETTING(0x04, "Mono RGB")
	PORT_CONFSETTING(0x08, "Colour composite")
	PORT_CONFSETTING(0x0C, "Television")
	PORT_CONFSETTING(0x10, "LCD")
	PORT_CONFNAME( 0xE0, 0x00, "CGA chipset")
	PORT_CONFSETTING(0x00, "IBM")
	PORT_CONFSETTING(0x20, "Amstrad PC1512")
	PORT_CONFSETTING(0x40, "Amstrad PPC512")
	PORT_CONFSETTING(0x60, "ATI")
	PORT_CONFSETTING(0x80, "Paradise")
INPUT_PORTS_END


static INPUT_PORTS_START( pc1512 )
	PORT_START( "cga_config" )
	PORT_CONFNAME( 0x03, 0x03, "CGA character set")
	PORT_CONFSETTING(0x00, "Greek")
	PORT_CONFSETTING(0x01, "Danish 2")
	PORT_CONFSETTING(0x02, "Danish 1")
	PORT_CONFSETTING(0x03, "Default")
	PORT_CONFNAME( 0x1C, 0x00, "CGA monitor type")
	PORT_CONFSETTING(0x00, "Colour RGB")
	PORT_CONFSETTING(0x04, "Mono RGB")
	PORT_BIT ( 0xE0, 0x20, IPT_UNUSED ) /* Chipset is always PC1512 */
INPUT_PORTS_END


/* Dipswitch for font selection */
#define CGA_FONT        (m_cga_config->read() & m_font_selection_mask)

/* Dipswitch for monitor selection */
#define CGA_MONITOR     (m_cga_config->read()&0x1C)
#define CGA_MONITOR_RGB         0x00    /* Colour RGB */
#define CGA_MONITOR_MONO        0x04    /* Greyscale RGB */
#define CGA_MONITOR_COMPOSITE   0x08    /* Colour composite */
#define CGA_MONITOR_TELEVISION  0x0C    /* Television */
#define CGA_MONITOR_LCD         0x10    /* LCD, eg PPC512 */


/* Dipswitch for chipset selection */
/* TODO: Get rid of this; these should be handled by separate classes */
#define CGA_CHIPSET     (m_cga_config->read() & 0xE0)
#define CGA_CHIPSET_IBM         0x00    /* Original IBM CGA */
#define CGA_CHIPSET_PC1512      0x20    /* PC1512 CGA subset */
#define CGA_CHIPSET_PC200       0x40    /* PC200 in CGA mode */
#define CGA_CHIPSET_ATI         0x60    /* ATI (supports Plantronics) */
#define CGA_CHIPSET_PARADISE    0x80    /* Paradise (used in PC1640) */


/* CGA palettes
 *
 * The first 16 are for RGB monitors
 * The next  16 are for greyscale modes
 * The next  16 are for text modes on colour composite
 * The next  16*16 are Mode 6 (colour composite) }
 * The next  64*16 are Mode 4 (colour composite) } both indexed by the CGA colour select register 0x3D9
 *
 */

const unsigned char cga_palette[16 * CGA_PALETTE_SETS][3] =
{
/* RGB colours */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xaa }, { 0x00, 0xaa, 0x00 }, { 0x00, 0xaa, 0xaa },
	{ 0xaa, 0x00, 0x00 }, { 0xaa, 0x00, 0xaa }, { 0xaa, 0x55, 0x00 }, { 0xaa, 0xaa, 0xaa },
	{ 0x55, 0x55, 0x55 }, { 0x55, 0x55, 0xff }, { 0x55, 0xff, 0x55 }, { 0x55, 0xff, 0xff },
	{ 0xff, 0x55, 0x55 }, { 0xff, 0x55, 0xff }, { 0xff, 0xff, 0x55 }, { 0xff, 0xff, 0xff },
/* Greyscale */
	{ 0x00, 0x00, 0x00 }, { 0x11, 0x11, 0x11 }, { 0x44, 0x44, 0x44 }, { 0x55, 0x55, 0x55 },
	{ 0x22, 0x22, 0x22 }, { 0x33, 0x33, 0x33 }, { 0x66, 0x66, 0x66 }, { 0x77, 0x77, 0x77 },
	{ 0x88, 0x88, 0x88 }, { 0x99, 0x99, 0x99 }, { 0xCC, 0xCC, 0xCC }, { 0xDD, 0xDD, 0xDD },
	{ 0xAA, 0xAA, 0xAA }, { 0xBB, 0xBB, 0xBB }, { 0xEE, 0xEE, 0xEE }, { 0xFF, 0xFF, 0xFF },
/* Text mode, composite monitor */
	{ 0x00, 0x00, 0x00 }, { 0x0E, 0x00, 0x7A }, { 0x07, 0x55, 0x00 }, { 0x02, 0x65, 0x39 },
	{ 0x51, 0x00, 0x1A }, { 0x54, 0x00, 0x76 }, { 0x48, 0x63, 0x00 }, { 0x8c, 0x8c, 0x8c },
	{ 0x38, 0x38, 0x38 }, { 0x58, 0x49, 0xD5 }, { 0x5F, 0xAD, 0x26 }, { 0x5B, 0xB9, 0xAC },
	{ 0xAA, 0x4A, 0x5E }, { 0xA7, 0x55, 0xD2 }, { 0xA2, 0xB9, 0x31 }, { 0xE2, 0xE2, 0xE2 },
/* Composite hi-res, colour reg = 0 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 },
/* Composite hi-res, colour reg = 1 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x04, 0x04 }, { 0x00, 0x00, 0x61 }, { 0x00, 0x00, 0x6b },
	{ 0x25, 0x00, 0x1E }, { 0x15, 0x00, 0x23 }, { 0x18, 0x00, 0x87 }, { 0x06, 0x00, 0x91 },
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x0b, 0x00 }, { 0x00, 0x00, 0x4C }, { 0x00, 0x02, 0x52 },
	{ 0x24, 0x00, 0x08 }, { 0x0E, 0x00, 0x0D }, { 0x18, 0x00, 0x6f }, { 0x07, 0x00, 0x7C },
/* Composite hi-res, colour reg = 2 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x28, 0x00 }, { 0x01, 0x00, 0x46 }, { 0x00, 0x21, 0x36 },
	{ 0x22, 0x00, 0x01 }, { 0x00, 0x21, 0x00 }, { 0x1b, 0x00, 0x43 }, { 0x00, 0x22, 0x33 },
	{ 0x07, 0x0D, 0x00 }, { 0x00, 0x4B, 0x00 }, { 0x04, 0x0E, 0x00 }, { 0x00, 0x57, 0x00 },
	{ 0x25, 0x02, 0x00 }, { 0x01, 0x46, 0x00 }, { 0x30, 0x04, 0x00 }, { 0x04, 0x53, 0x00 },
/* Composite hi-res, colour reg = 3 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x30, 0x00 }, { 0x00, 0x00, 0x8E }, { 0x00, 0x38, 0x87 },
	{ 0x2E, 0x00, 0x01 }, { 0x00, 0x21, 0x00 }, { 0x22, 0x00, 0x8C }, { 0x00, 0x35, 0x95 },
	{ 0x00, 0x0F, 0x00 }, { 0x00, 0x4F, 0x00 }, { 0x00, 0x0B, 0x3F }, { 0x00, 0x62, 0x45 },
	{ 0x29, 0x00, 0x00 }, { 0x00, 0x4E, 0x00 }, { 0x35, 0x04, 0x48 }, { 0x01, 0x62, 0x49 },
/* Composite hi-res, colour reg = 4 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x06, 0x02 }, { 0x01, 0x00, 0x1f }, { 0x00, 0x00, 0x24 },
	{ 0x54, 0x00, 0x38 }, { 0x25, 0x00, 0x23 }, { 0x3A, 0x00, 0x4f }, { 0x29, 0x00, 0x56 },
	{ 0x10, 0x03, 0x00 }, { 0x06, 0x08, 0x00 }, { 0x15, 0x00, 0x00 }, { 0x02, 0x03, 0x00 },
	{ 0x82, 0x00, 0x00 }, { 0x49, 0x00, 0x00 }, { 0x5B, 0x00, 0x0b }, { 0x52, 0x00, 0x0c },
/* Composite hi-res, colour reg = 5 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x2c, 0x00 }, { 0x06, 0x01, 0x57 }, { 0x00, 0x22, 0x42 },
	{ 0x33, 0x00, 0x01 }, { 0x00, 0x26, 0x00 }, { 0x3a, 0x00, 0x54 }, { 0x08, 0x1D, 0x54 },
	{ 0x13, 0x17, 0x00 }, { 0x00, 0x64, 0x00 }, { 0x29, 0x15, 0x00 }, { 0x00, 0x64, 0x00 },
	{ 0x59, 0x0A, 0x00 }, { 0x30, 0x61, 0x00 }, { 0x7A, 0x06, 0x00 }, { 0x4A, 0x64, 0x00 },
/* Composite hi-res, colour reg = 6 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x33, 0x00 }, { 0x06, 0x00, 0x5E }, { 0x00, 0x22, 0x45 },
	{ 0x34, 0x00, 0x04 }, { 0x00, 0x1e, 0x00 }, { 0x3d, 0x00, 0x4c }, { 0x0c, 0x22, 0x58 },
	{ 0x18, 0x19, 0x00 }, { 0x00, 0x62, 0x00 }, { 0x2b, 0x14, 0x00 }, { 0x01, 0x64, 0x00 },
	{ 0x57, 0x0f, 0x00 }, { 0x29, 0x63, 0x00 }, { 0x78, 0x09, 0x00 }, { 0x51, 0x61, 0x00 },
/* Composite hi-res, colour reg = 7 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x3C, 0x01 }, { 0x04, 0x00, 0xC5 }, { 0x00, 0x4C, 0xC7 },
	{ 0x6A, 0x00, 0x15 }, { 0x28, 0x28, 0x24 }, { 0x8A, 0x00, 0xF8 }, { 0x70, 0x61, 0xFF },
	{ 0x20, 0x33, 0x00 }, { 0x00, 0x85, 0x00 }, { 0x2E, 0x25, 0x28 }, { 0x00, 0x98, 0x3B },
	{ 0xb1, 0x11, 0x00 }, { 0x6A, 0x75, 0x00 }, { 0xcc, 0x16, 0x81 }, { 0x91, 0x8e, 0x91 },
/* Composite hi-res, colour reg = 8 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x12, 0x0F }, { 0x04, 0x00, 0x5F }, { 0x00, 0x02, 0x67 },
	{ 0x31, 0x00, 0x01 }, { 0x04, 0x01, 0x04 }, { 0x37, 0x00, 0x52 }, { 0x17, 0x00, 0x6d },
	{ 0x00, 0x10, 0x00 }, { 0x00, 0x29, 0x00 }, { 0x04, 0x03, 0x04 }, { 0x00, 0x24, 0x16 },
	{ 0x2f, 0x00, 0x00 }, { 0x07, 0x23, 0x00 }, { 0x43, 0x00, 0x08 }, { 0x25, 0x23, 0x24 },
/* Composite hi-res, colour reg = 9 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x19, 0x14 }, { 0x00, 0x00, 0xc2 }, { 0x00, 0x1c, 0xed },
	{ 0x5e, 0x00, 0x13 }, { 0x2c, 0x03, 0x3a }, { 0x78, 0x00, 0xfa }, { 0x49, 0x11, 0xff },
	{ 0x00, 0x15, 0x00 }, { 0x00, 0x40, 0x00 }, { 0x0d, 0x11, 0x68 }, { 0x00, 0x4f, 0x9c },
	{ 0x67, 0x00, 0x00 }, { 0x39, 0x36, 0x00 }, { 0x91, 0x05, 0xa6 }, { 0x62, 0x45, 0xdc },
/* Composite hi-res, colour reg = A */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x4B, 0x08 }, { 0x05, 0x00, 0xAA }, { 0x00, 0x50, 0xc7 },
	{ 0x58, 0x00, 0x06 }, { 0x05, 0x44, 0x06 }, { 0x75, 0x00, 0xb0 }, { 0x2e, 0x4f, 0xdc },
	{ 0x0c, 0x2f, 0x00 }, { 0x00, 0xa7, 0x00 }, { 0x26, 0x2e, 0x03 }, { 0x00, 0xb4, 0x24 },
	{ 0x84, 0x1b, 0x00 }, { 0x2d, 0xa5, 0x00 }, { 0xa5, 0x2a, 0x16 }, { 0x5f, 0xb2, 0x2a },
/* Composite hi-res, colour reg = B */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x59, 0x07 }, { 0x00, 0x08, 0xf0 }, { 0x00, 0x06, 0xfd },
	{ 0x69, 0x00, 0x09 }, { 0x0d, 0x4c, 0x10 }, { 0x8f, 0x00, 0xf4 }, { 0x38, 0x66, 0xff },
	{ 0x02, 0x27, 0x00 }, { 0x00, 0xac, 0x00 }, { 0x19, 0x2f, 0x6d }, { 0x00, 0xc5, 0x82 },
	{ 0x7b, 0x18, 0x00 }, { 0x30, 0xa7, 0x00 }, { 0xac, 0x2b, 0x81 }, { 0x5b, 0xc0, 0xa4 },
/* Composite hi-res, colour reg = C */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x1C, 0x0C }, { 0x0a, 0x00, 0x7c }, { 0x00, 0x0d, 0x8f },
	{ 0x6e, 0x00, 0x18 }, { 0x48, 0x02, 0x4a }, { 0x95, 0x00, 0xc3 }, { 0x68, 0x01, 0xef },
	{ 0x12, 0x1d, 0x00 }, { 0x00, 0x53, 0x00 }, { 0x33, 0x21, 0x00 }, { 0x05, 0x52, 0x13 },
	{ 0xb4, 0x09, 0x00 }, { 0x87, 0x41, 0x00 }, { 0xd8, 0x07, 0x3a }, { 0xb0, 0x49, 0x63 },
/* Composite hi-res, colour reg = D */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x1d, 0x17 }, { 0x00, 0x08, 0xcf }, { 0x00, 0x1b, 0xf2 },
	{ 0x83, 0x00, 0x30 }, { 0x4c, 0x08, 0x53 }, { 0xae, 0x00, 0xfa }, { 0x85, 0x0b, 0xff },
	{ 0x09, 0x19, 0x00 }, { 0x00, 0x57, 0x00 }, { 0x21, 0x15, 0x4f }, { 0x00, 0x5e, 0x89 },
	{ 0xb0, 0x04, 0x00 }, { 0x76, 0x4e, 0x00 }, { 0xe2, 0x0a, 0xa9 }, { 0xae, 0x56, 0xe1 },
/* Composite hi-res, colour reg = E */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x57, 0x06 }, { 0x17, 0x00, 0xc3 }, { 0x00, 0x55, 0xd9 },
	{ 0x6f, 0x00, 0x06 }, { 0x18, 0x49, 0x0d }, { 0xa4, 0x00, 0xcd }, { 0x4e, 0x4c, 0xf7 },
	{ 0x1c, 0x3f, 0x00 }, { 0x00, 0xbf, 0x00 }, { 0x51, 0x35, 0x00 }, { 0x06, 0xc4, 0x1b },
	{ 0xb6, 0x2d, 0x00 }, { 0x73, 0xb2, 0x00 }, { 0xf5, 0x30, 0x21 }, { 0xaa, 0xbf, 0x2f },
/* Composite hi-res, colour reg = F */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x68, 0x10 }, { 0x10, 0x00, 0xff }, { 0x00, 0x7c, 0xFF },
	{ 0xb3, 0x00, 0x2A }, { 0x53, 0x55, 0x51 }, { 0xf0, 0x00, 0xff }, { 0x95, 0x72, 0xff },
	{ 0x25, 0x3e, 0x00 }, { 0x00, 0xda, 0x00 }, { 0x58, 0x52, 0x56 }, { 0x00, 0xf8, 0x7f },
	{ 0xf8, 0x2c, 0x00 }, { 0xa8, 0xcf, 0x00 }, { 0xff, 0x41, 0xb8 }, { 0xed, 0xea, 0xed },
/* Composite lo-res, colour reg = 0 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x26, 0x34 }, { 0x00, 0x00, 0x24 }, { 0x00, 0x25, 0x46 },
	{ 0x29, 0x03, 0x00 }, { 0x04, 0x55, 0x00 }, { 0x1f, 0x0c, 0x00 }, { 0x0e, 0x53, 0x03 },
	{ 0x50, 0x00, 0x00 }, { 0x33, 0x36, 0x2b }, { 0x51, 0x00, 0x0b }, { 0x43, 0x37, 0x44 },
	{ 0x60, 0x07, 0x00 }, { 0x3c, 0x61, 0x00 }, { 0x59, 0x1c, 0x00 }, { 0x4a, 0x64, 0x00 },
/* Composite lo-res, colour reg = 1 */
	{ 0x07, 0x00, 0x7d }, { 0x00, 0x21, 0x4e }, { 0x15, 0x00, 0x36 }, { 0x04, 0x19, 0x77 },
	{ 0x16, 0x1a, 0x00 }, { 0x04, 0x55, 0x00 }, { 0x23, 0x0b, 0x00 }, { 0x13, 0x51, 0x03 },
	{ 0x41, 0x02, 0x3e }, { 0x2e, 0x33, 0x24 }, { 0x51, 0x00, 0x14 }, { 0x41, 0x33, 0x46 },
	{ 0x51, 0x2b, 0x00 }, { 0x3f, 0x60, 0x00 }, { 0x60, 0x17, 0x00 }, { 0x4d, 0x61, 0x00 },
/* Composite lo-res, colour reg = 2 */
	{ 0x03, 0x55, 0x00 }, { 0x03, 0x55, 0x00 }, { 0x21, 0x0c, 0x00 }, { 0x11, 0x51, 0x03 },
	{ 0x03, 0x55, 0x00 }, { 0x03, 0x55, 0x00 }, { 0x21, 0x0c, 0x00 }, { 0x11, 0x51, 0x03 },
	{ 0x31, 0x37, 0x29 }, { 0x30, 0x36, 0x2a }, { 0x51, 0x00, 0x11 }, { 0x41, 0x34, 0x46 },
	{ 0x3c, 0x63, 0x00 }, { 0x3d, 0x63, 0x00 }, { 0x5f, 0x17, 0x00 }, { 0x4d, 0x61, 0x00 },
/* Composite lo-res, colour reg = 3 */
	{ 0x04, 0x61, 0x4e }, { 0x05, 0x49, 0x02 }, { 0x1f, 0x04, 0x00 }, { 0x12, 0x47, 0x13 },
	{ 0x03, 0x68, 0x2f }, { 0x05, 0x54, 0x00 }, { 0x1e, 0x0e, 0x00 }, { 0x0f, 0x51, 0x01 },
	{ 0x26, 0x46, 0x73 }, { 0x2f, 0x34, 0x27 }, { 0x50, 0x00, 0x0b }, { 0x48, 0x31, 0x47 },
	{ 0x3e, 0x70, 0x1e }, { 0x40, 0x5f, 0x00 }, { 0x57, 0x1d, 0x00 }, { 0x4a, 0x62, 0x00 },
/* Composite lo-res, colour reg = 4 */
	{ 0x52, 0x00, 0x14 }, { 0x2e, 0x32, 0x25 }, { 0x52, 0x00, 0x14 }, { 0x46, 0x2f, 0x47 },
	{ 0x1f, 0x09, 0x00 }, { 0x04, 0x55, 0x00 }, { 0x21, 0x0e, 0x00 }, { 0x11, 0x50, 0x02 },
	{ 0x52, 0x00, 0x14 }, { 0x2d, 0x33, 0x25 }, { 0x52, 0x00, 0x14 }, { 0x40, 0x36, 0x3f },
	{ 0x5c, 0x18, 0x00 }, { 0x40, 0x5f, 0x00 }, { 0x5e, 0x19, 0x00 }, { 0x4b, 0x62, 0x00 },
/* Composite lo-res, colour reg = 5 */
	{ 0x51, 0x00, 0x81 }, { 0x2a, 0x2a, 0x3f }, { 0x4f, 0x00, 0x1c }, { 0x3b, 0x2b, 0x5c },
	{ 0x22, 0x1b, 0x13 }, { 0x04, 0x55, 0x00 }, { 0x21, 0x0e, 0x00 }, { 0x0e, 0x52, 0x04 },
	{ 0x4c, 0x03, 0x59 }, { 0x2e, 0x32, 0x25 }, { 0x51, 0x00, 0x0b }, { 0x3e, 0x37, 0x3d },
	{ 0x5d, 0x2a, 0x03 }, { 0x3d, 0x60, 0x00 }, { 0x5d, 0x19, 0x00 }, { 0x4a, 0x63, 0x00 },
/* Composite lo-res, colour reg = 6 */
	{ 0x4b, 0x60, 0x00 }, { 0x41, 0x5f, 0x00 }, { 0x5b, 0x1a, 0x00 }, { 0x4b, 0x60, 0x00 },
	{ 0x0e, 0x51, 0x03 }, { 0x03, 0x55, 0x00 }, { 0x22, 0x0b, 0x00 }, { 0x12, 0x51, 0x03 },
	{ 0x41, 0x34, 0x47 }, { 0x31, 0x37, 0x29 }, { 0x50, 0x00, 0x10 }, { 0x3f, 0x32, 0x43 },
	{ 0x4b, 0x60, 0x00 }, { 0x3d, 0x61, 0x00 }, { 0x62, 0x16, 0x00 }, { 0x4b, 0x60, 0x00 },
/* Composite lo-res, colour reg = 7 */
	{ 0x8b, 0x8b, 0x8b }, { 0x83, 0x5b, 0x00 }, { 0xa4, 0x1b, 0x00 }, { 0x92, 0x5a, 0x09 },
	{ 0x07, 0x79, 0x6f }, { 0x06, 0x55, 0x00 }, { 0x1f, 0x0d, 0x00 }, { 0x10, 0x52, 0x01 },
	{ 0x23, 0x62, 0xa4 }, { 0x2b, 0x33, 0x29 }, { 0x51, 0x00, 0x11 }, { 0x40, 0x36, 0x42 },
	{ 0x46, 0x86, 0x63 }, { 0x42, 0x5e, 0x00 }, { 0x5e, 0x17, 0x00 }, { 0x4a, 0x62, 0x00 },
/* Composite lo-res, colour reg = 8 */
	{ 0x26, 0x26, 0x26 }, { 0x0a, 0x49, 0x00 }, { 0x25, 0x07, 0x00 }, { 0x16, 0x4c, 0x0e },
	{ 0x1c, 0x29, 0x12 }, { 0x06, 0x55, 0x00 }, { 0x21, 0x0c, 0x00 }, { 0x11, 0x51, 0x02 },
	{ 0x4d, 0x10, 0x5f }, { 0x2c, 0x33, 0x26 }, { 0x51, 0x00, 0x0f }, { 0x41, 0x35, 0x47 },
	{ 0x5a, 0x35, 0x00 }, { 0x43, 0x5f, 0x00 }, { 0x5f, 0x15, 0x00 }, { 0x4d, 0x62, 0x00 },
/* Composite lo-res, colour reg = 9 */
	{ 0x92, 0x47, 0xd3 }, { 0x47, 0x47, 0x1b }, { 0x66, 0x00, 0x09 }, { 0x54, 0x44, 0x37 },
	{ 0x15, 0x4b, 0x8a }, { 0x05, 0x55, 0x00 }, { 0x00, 0x10, 0x00 }, { 0x10, 0x52, 0x02 },
	{ 0x40, 0x33, 0xd4 }, { 0x2f, 0x33, 0x26 }, { 0x51, 0x00, 0x0d }, { 0x3e, 0x37, 0x3e },
	{ 0x51, 0x59, 0x75 }, { 0x3b, 0x63, 0x00 }, { 0x5b, 0x1a, 0x00 }, { 0x49, 0x64, 0x00 },
/* Composite lo-res, colour reg = A */
	{ 0x57, 0xac, 0x33 }, { 0x54, 0x7f, 0x00 }, { 0x7f, 0x2e, 0x00 }, { 0x6a, 0x77, 0x00 },
	{ 0x05, 0x80, 0x70 }, { 0x03, 0x54, 0x00 }, { 0x22, 0x0c, 0x00 }, { 0x13, 0x52, 0x00 },
	{ 0x31, 0x64, 0xbe }, { 0x30, 0x35, 0x2a }, { 0x52, 0x00, 0x12 }, { 0x41, 0x33, 0x46 },
	{ 0x3c, 0x91, 0x50 }, { 0x3c, 0x62, 0x00 }, { 0x60, 0x15, 0x00 }, { 0x4f, 0x61, 0x00 },
/* Composite lo-res, colour reg = B */
	{ 0x5b, 0xb9, 0xa7 }, { 0x5b, 0x6d, 0x00 }, { 0x7f, 0x29, 0x00 }, { 0x6c, 0x6e, 0x00 },
	{ 0x05, 0x95, 0xcb }, { 0x04, 0x54, 0x00 }, { 0x23, 0x0a, 0x00 }, { 0x12, 0x51, 0x02 },
	{ 0x28, 0x77, 0xfb }, { 0x32, 0x37, 0x2f }, { 0x52, 0x00, 0x12 }, { 0x3e, 0x34, 0x40 },
	{ 0x3a, 0xa3, 0xaf }, { 0x3c, 0x63, 0x00 }, { 0x60, 0x15, 0x00 }, { 0x50, 0x61, 0x00 },
/* Composite lo-res, colour reg = C */
	{ 0xaa, 0x45, 0x6a }, { 0x8c, 0x59, 0x00 }, { 0xa8, 0x1a, 0x00 }, { 0x96, 0x60, 0x05 },
	{ 0x20, 0x35, 0x41 }, { 0x03, 0x55, 0x00 }, { 0x22, 0x0b, 0x00 }, { 0x10, 0x52, 0x02 },
	{ 0x4f, 0x1e, 0xa2 }, { 0x2e, 0x34, 0x25 }, { 0x50, 0x00, 0x10 }, { 0x42, 0x36, 0x45 },
	{ 0x56, 0x48, 0x2a }, { 0x41, 0x5e, 0x00 }, { 0x5d, 0x19, 0x00 }, { 0x49, 0x64, 0x00 },
/* Composite lo-res, colour reg = D */
	{ 0xa9, 0x54, 0xd6 }, { 0x85, 0x52, 0x09 }, { 0xa5, 0x17, 0x00 }, { 0x96, 0x52, 0x23 },
	{ 0x1e, 0x48, 0x9f }, { 0x06, 0x55, 0x00 }, { 0x1f, 0x0c, 0x00 }, { 0x0f, 0x52, 0x01 },
	{ 0x46, 0x35, 0xe1 }, { 0x2b, 0x32, 0x26 }, { 0x51, 0x00, 0x0e }, { 0x3e, 0x39, 0x3e },
	{ 0x5d, 0x58, 0x88 }, { 0x41, 0x60, 0x00 }, { 0x57, 0x1c, 0x00 }, { 0x4a, 0x62, 0x00 },
/* Composite lo-res, colour reg = E */
	{ 0xa4, 0xbb, 0x30 }, { 0x9d, 0x84, 0x00 }, { 0xb6, 0x3f, 0x00 }, { 0xa1, 0x8c, 0x00 },
	{ 0x14, 0x7b, 0x8a }, { 0x06, 0x55, 0x00 }, { 0x21, 0x0b, 0x00 }, { 0x13, 0x51, 0x02 },
	{ 0x3f, 0x67, 0xd5 }, { 0x2d, 0x36, 0x29 }, { 0x52, 0x00, 0x11 }, { 0x41, 0x33, 0x46 },
	{ 0x4c, 0x8e, 0x6e }, { 0x3e, 0x61, 0x00 }, { 0x5f, 0x16, 0x00 }, { 0x4c, 0x61, 0x00 },
/* Composite lo-res, colour reg = F */
	{ 0xe3, 0xe3, 0xe3 }, { 0xdb, 0x82, 0x00 }, { 0xf5, 0x43, 0x00 }, { 0xee, 0x83, 0x00 },
	{ 0x08, 0xa6, 0xf5 }, { 0x04, 0x53, 0x00 }, { 0x1c, 0x0d, 0x00 }, { 0x13, 0x52, 0x00 },
	{ 0x25, 0x91, 0xfc }, { 0x2c, 0x35, 0x30 }, { 0x51, 0x00, 0x0e }, { 0x3b, 0x36, 0x38 },
	{ 0x43, 0xb5, 0xf7 }, { 0x3b, 0x62, 0x00 }, { 0x56, 0x1c, 0x00 }, { 0x4d, 0x61, 0x00 },
/* Composite lo-res, colour reg = 10 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x58, 0xb9 }, { 0x00, 0x11, 0x8a }, { 0x00, 0x4a, 0xe1 },
	{ 0x73, 0x22, 0x00 }, { 0x5a, 0xad, 0x2e }, { 0x78, 0x61, 0x0d }, { 0x6a, 0xa9, 0x4c },
	{ 0xac, 0x0a, 0x00 }, { 0x87, 0x8d, 0x80 }, { 0xab, 0x46, 0x6c }, { 0x95, 0x8b, 0x97 },
	{ 0xac, 0x30, 0x00 }, { 0x91, 0xbc, 0x16 }, { 0xb8, 0x6d, 0x07 }, { 0xa7, 0xb9, 0x2d },
/* Composite lo-res, colour reg = 11 */
	{ 0x60, 0x00, 0x7c }, { 0x01, 0x54, 0xdb }, { 0x09, 0x11, 0xb9 }, { 0x07, 0x47, 0xf8 },
	{ 0x76, 0x46, 0x02 }, { 0x5a, 0xae, 0x2e }, { 0x78, 0x62, 0x0c }, { 0x68, 0xa9, 0x4b },
	{ 0x99, 0x2a, 0x09 }, { 0x87, 0x8d, 0x80 }, { 0xab, 0x46, 0x6b }, { 0x93, 0x89, 0x95 },
	{ 0xa4, 0x54, 0x00 }, { 0x93, 0xbb, 0x16 }, { 0xb9, 0x6b, 0x04 }, { 0xa4, 0xb9, 0x30 },
/* Composite lo-res, colour reg = 12 */
	{ 0x07, 0x55, 0x00 }, { 0x03, 0x83, 0x70 }, { 0x1c, 0x3a, 0x42 }, { 0x0e, 0x81, 0x82 },
	{ 0x58, 0x7e, 0x00 }, { 0x5b, 0xad, 0x2f }, { 0x77, 0x60, 0x0f }, { 0x67, 0xac, 0x49 },
	{ 0x87, 0x5a, 0x00 }, { 0x89, 0x8c, 0x81 }, { 0xa9, 0x49, 0x5e }, { 0x9b, 0x8a, 0x96 },
	{ 0x9f, 0x83, 0x00 }, { 0x94, 0xb9, 0x19 }, { 0xb0, 0x72, 0x03 }, { 0xa5, 0xbb, 0x30 },
/* Composite lo-res, colour reg = 13 */
	{ 0x03, 0x63, 0x48 }, { 0x04, 0x76, 0x8c }, { 0x1d, 0x34, 0x5a }, { 0x0d, 0x7a, 0x9c },
	{ 0x5a, 0x8e, 0x03 }, { 0x58, 0xac, 0x33 }, { 0x76, 0x60, 0x0b }, { 0x68, 0xaa, 0x4b },
	{ 0x7e, 0x6e, 0x3b }, { 0x88, 0x8c, 0x80 }, { 0xaa, 0x48, 0x64 }, { 0x94, 0x91, 0x92 },
	{ 0x94, 0x9b, 0x00 }, { 0x96, 0xb9, 0x16 }, { 0xb0, 0x73, 0x01 }, { 0xa7, 0xb8, 0x2e },
/* Composite lo-res, colour reg = 14 */
	{ 0x52, 0x00, 0x13 }, { 0x29, 0x61, 0xb6 }, { 0x52, 0x1e, 0xa1 }, { 0x41, 0x63, 0xdb },
	{ 0x7b, 0x2f, 0x00 }, { 0x5d, 0xac, 0x2c }, { 0x77, 0x63, 0x0a }, { 0x67, 0xa9, 0x51 },
	{ 0xaf, 0x18, 0x00 }, { 0x83, 0x8a, 0x7d }, { 0xa9, 0x46, 0x66 }, { 0x9a, 0x8c, 0xa0 },
	{ 0xb1, 0x43, 0x00 }, { 0x9a, 0xb7, 0x19 }, { 0xb7, 0x6e, 0x05 }, { 0xa4, 0xb9, 0x2f },
/* Composite lo-res, colour reg = 15 */
	{ 0x52, 0x00, 0x7a }, { 0x2e, 0x55, 0xdc }, { 0x4e, 0x1b, 0xb1 }, { 0x3c, 0x55, 0xec },
	{ 0x80, 0x3f, 0x00 }, { 0x5b, 0xad, 0x2e }, { 0x73, 0x61, 0x0a }, { 0x66, 0xaa, 0x50 },
	{ 0xa7, 0x29, 0x29 }, { 0x86, 0x8a, 0x7d }, { 0xa8, 0x48, 0x60 }, { 0x98, 0x8e, 0x9b },
	{ 0xc0, 0x4a, 0x00 }, { 0x9a, 0xb5, 0x18 }, { 0xb3, 0x72, 0x06 }, { 0xa2, 0xba, 0x31 },
/* Composite lo-res, colour reg = 16 */
	{ 0x4d, 0x61, 0x00 }, { 0x3b, 0x91, 0x53 }, { 0x59, 0x46, 0x2c }, { 0x48, 0x95, 0x63 },
	{ 0x6c, 0x77, 0x00 }, { 0x5a, 0xac, 0x31 }, { 0x75, 0x63, 0x09 }, { 0x66, 0xa9, 0x4e },
	{ 0x8e, 0x6a, 0x0f }, { 0x87, 0x8b, 0x7f }, { 0xa9, 0x47, 0x66 }, { 0x9b, 0x8c, 0x9f },
	{ 0xab, 0x86, 0x00 }, { 0x9a, 0xb6, 0x18 }, { 0xae, 0x74, 0x01 }, { 0xa2, 0xba, 0x2f },
/* Composite lo-res, colour reg = 17 */
	{ 0x8b, 0x8b, 0x8b }, { 0x7f, 0x89, 0x79 }, { 0xa4, 0x4a, 0x5c }, { 0x96, 0x8a, 0x95 },
	{ 0x5c, 0xa1, 0x36 }, { 0x5d, 0xad, 0x2b }, { 0x77, 0x62, 0x0a }, { 0x68, 0xa8, 0x4f },
	{ 0x83, 0x88, 0x6f }, { 0x85, 0x8d, 0x81 }, { 0xa9, 0x46, 0x69 }, { 0x99, 0x8b, 0x9f },
	{ 0x97, 0xb1, 0x22 }, { 0x99, 0xb7, 0x18 }, { 0xb8, 0x6c, 0x04 }, { 0xa2, 0xba, 0x2e },
/* Composite lo-res, colour reg = 18 */
	{ 0x25, 0x25, 0x25 }, { 0x0b, 0x78, 0x8b }, { 0x25, 0x34, 0x5a }, { 0x14, 0x7d, 0x9d },
	{ 0x76, 0x4f, 0x00 }, { 0x5a, 0xac, 0x2e }, { 0x74, 0x64, 0x07 }, { 0x66, 0xaa, 0x49 },
	{ 0xa7, 0x37, 0x25 }, { 0x87, 0x8b, 0x80 }, { 0xa8, 0x48, 0x64 }, { 0x9a, 0x8f, 0x9a },
	{ 0xb6, 0x5a, 0x00 }, { 0x96, 0xba, 0x17 }, { 0xae, 0x73, 0x01 }, { 0xa2, 0xba, 0x30 },
/* Composite lo-res, colour reg = 19 */
	{ 0x5d, 0x48, 0xd5 }, { 0x4a, 0x77, 0xb3 }, { 0x65, 0x35, 0x86 }, { 0x4d, 0x77, 0xc2 },
	{ 0x6f, 0x72, 0x53 }, { 0x5a, 0xac, 0x30 }, { 0x75, 0x62, 0x09 }, { 0x68, 0xa9, 0x48 },
	{ 0x9c, 0x57, 0xa1 }, { 0x87, 0x8b, 0x80 }, { 0xa7, 0x49, 0x62 }, { 0x92, 0x90, 0x92 },
	{ 0xab, 0x7d, 0x3a }, { 0x97, 0xb8, 0x17 }, { 0xb0, 0x74, 0x03 }, { 0xa2, 0xba, 0x2e },
/* Composite lo-res, colour reg = 1A */
	{ 0x59, 0xad, 0x2e }, { 0x59, 0xad, 0x2e }, { 0x75, 0x64, 0x08 }, { 0x69, 0xa7, 0x4d },
	{ 0x59, 0xad, 0x2e }, { 0x59, 0xad, 0x2e }, { 0x75, 0x64, 0x08 }, { 0x69, 0xa7, 0x4d },
	{ 0x87, 0x8d, 0x82 }, { 0x85, 0x8b, 0x7d }, { 0xa9, 0x47, 0x67 }, { 0x99, 0x8c, 0x9d },
	{ 0x94, 0xba, 0x17 }, { 0x94, 0xba, 0x17 }, { 0xb6, 0x6e, 0x06 }, { 0xa2, 0xbb, 0x30 },
/* Composite lo-res, colour reg = 1B */
	{ 0x5b, 0xb9, 0xa6 }, { 0x5c, 0xa2, 0x4a }, { 0x7a, 0x5c, 0x24 }, { 0x6a, 0x9a, 0x6c },
	{ 0x56, 0xbf, 0x8e }, { 0x59, 0xae, 0x31 }, { 0x78, 0x60, 0x0d }, { 0x68, 0xa9, 0x4f },
	{ 0x7f, 0xa3, 0xcd }, { 0x85, 0x8c, 0x80 }, { 0xaa, 0x47, 0x6a }, { 0x98, 0x8b, 0x9c },
	{ 0x93, 0xcd, 0x72 }, { 0x92, 0xbd, 0x14 }, { 0xb8, 0x6c, 0x06 }, { 0xa4, 0xb9, 0x2f },
/* Composite lo-res, colour reg = 1C */
	{ 0xa9, 0x44, 0x63 }, { 0x85, 0x8a, 0x7f }, { 0xa9, 0x44, 0x63 }, { 0x99, 0x8e, 0x9d },
	{ 0x74, 0x5f, 0x0d }, { 0x5c, 0xad, 0x2c }, { 0x77, 0x63, 0x0a }, { 0x68, 0xa8, 0x4e },
	{ 0xa9, 0x44, 0x63 }, { 0x84, 0x8b, 0x7e }, { 0xa9, 0x44, 0x63 }, { 0x99, 0x8c, 0x9e },
	{ 0xad, 0x72, 0x01 }, { 0x9b, 0xb6, 0x1a }, { 0xb3, 0x6e, 0x05 }, { 0xa4, 0xb9, 0x2f },
/* Composite lo-res, colour reg = 1D */
	{ 0xaa, 0x55, 0xd4 }, { 0x83, 0x81, 0x9b }, { 0xa6, 0x43, 0x7b }, { 0x95, 0x80, 0xbd },
	{ 0x76, 0x72, 0x66 }, { 0x5a, 0xad, 0x2c }, { 0x7b, 0x61, 0x0c }, { 0x68, 0xa9, 0x50 },
	{ 0xa5, 0x59, 0xaa }, { 0x87, 0x8e, 0x7f }, { 0xa9, 0x45, 0x6a }, { 0x97, 0x8b, 0x98 },
	{ 0xb2, 0x82, 0x48 }, { 0x93, 0xbb, 0x16 }, { 0xb9, 0x6d, 0x05 }, { 0xa4, 0xb9, 0x2f },
/* Composite lo-res, colour reg = 1E */
	{ 0xa5, 0xb8, 0x2d }, { 0xa5, 0xb8, 0x2d }, { 0xb4, 0x70, 0x05 }, { 0xa5, 0xb8, 0x2d },
	{ 0x64, 0xaa, 0x4e }, { 0x5b, 0xad, 0x2c }, { 0x77, 0x63, 0x0b }, { 0x68, 0xa8, 0x4f },
	{ 0x94, 0x91, 0x95 }, { 0x83, 0x8a, 0x7b }, { 0xa9, 0x47, 0x67 }, { 0x98, 0x8a, 0x9e },
	{ 0xa5, 0xb8, 0x2d }, { 0x9a, 0xb6, 0x1a }, { 0xb2, 0x70, 0x05 }, { 0xa5, 0xb8, 0x2d },
/* Composite lo-res, colour reg = 1F */
	{ 0xe3, 0xe3, 0xe3 }, { 0xde, 0xb1, 0x45 }, { 0xf8, 0x71, 0x3e }, { 0xeb, 0xb3, 0x5e },
	{ 0x58, 0xd3, 0xc4 }, { 0x5b, 0xad, 0x2d }, { 0x78, 0x63, 0x0b }, { 0x68, 0xa8, 0x4f },
	{ 0x7f, 0xb7, 0xf4 }, { 0x86, 0x8b, 0x7d }, { 0xa8, 0x46, 0x69 }, { 0x9a, 0x8c, 0x9f },
	{ 0x99, 0xe0, 0xbc }, { 0x99, 0xb6, 0x1a }, { 0xb8, 0x6d, 0x07 }, { 0xa5, 0xb8, 0x30 },
/* Composite lo-res, colour reg = 20 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x3a, 0x85 }, { 0x00, 0x00, 0x6c }, { 0x00, 0x46, 0xce },
	{ 0x26, 0x01, 0x00 }, { 0x03, 0x61, 0x4a }, { 0x24, 0x16, 0x2a }, { 0x09, 0x72, 0x8d },
	{ 0x4d, 0x00, 0x00 }, { 0x2d, 0x45, 0x9c }, { 0x51, 0x00, 0x7c }, { 0x30, 0x58, 0xe1 },
	{ 0x9e, 0x0f, 0x00 }, { 0x86, 0x7b, 0x45 }, { 0xab, 0x29, 0x2c }, { 0x8b, 0x89, 0x88 },
/* Composite lo-res, colour reg = 21 */
	{ 0x06, 0x00, 0x7C }, { 0x00, 0x3B, 0xA0 }, { 0x14, 0x00, 0x93 }, { 0x00, 0x49, 0xF7 },
	{ 0x19, 0x12, 0x13 }, { 0x02, 0x63, 0x3f }, { 0x25, 0x16, 0x2b }, { 0x09, 0x71, 0x93 },
	{ 0x46, 0x00, 0x65 }, { 0x28, 0x45, 0x93 }, { 0x50, 0x00, 0x80 }, { 0x32, 0x55, 0xe6 },
	{ 0x9c, 0x2d, 0x0c }, { 0x86, 0x78, 0x44 }, { 0xaa, 0x29, 0x33 }, { 0x92, 0x84, 0x84 },
/* Composite lo-res, colour reg = 22 */
	{ 0x05, 0x56, 0x00 }, { 0x04, 0x69, 0x32 }, { 0x21, 0x1d, 0x13 }, { 0x07, 0x76, 0x7e },
	{ 0x05, 0x48, 0x03 }, { 0x03, 0x64, 0x43 }, { 0x24, 0x16, 0x28 }, { 0x08, 0x70, 0x92 },
	{ 0x45, 0x41, 0x74 }, { 0x27, 0x44, 0x92 }, { 0x4f, 0x00, 0x7f }, { 0x36, 0x58, 0xe8 },
	{ 0x85, 0x57, 0x02 }, { 0x87, 0x77, 0x45 }, { 0xa6, 0x2c, 0x2c }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 23 */
	{ 0x02, 0x61, 0x49 }, { 0x02, 0x61, 0x49 }, { 0x24, 0x15, 0x27 }, { 0x05, 0x73, 0x84 },
	{ 0x02, 0x61, 0x49 }, { 0x02, 0x61, 0x49 }, { 0x24, 0x15, 0x27 }, { 0x05, 0x73, 0x84 },
	{ 0x2a, 0x43, 0x96 }, { 0x2a, 0x43, 0x96 }, { 0x51, 0x00, 0x7d }, { 0x31, 0x5a, 0xdc },
	{ 0x86, 0x79, 0x3d }, { 0x86, 0x79, 0x3d }, { 0xa8, 0x2a, 0x21 }, { 0x8a, 0x8a, 0x8a },
/* Composite lo-res, colour reg = 24 */
	{ 0x51, 0x00, 0x0e }, { 0x2b, 0x49, 0x76 }, { 0x4c, 0x04, 0x53 }, { 0x23, 0x5a, 0xf5 },
	{ 0x22, 0x05, 0x00 }, { 0x04, 0x06, 0x4b }, { 0x22, 0x13, 0x22 }, { 0x03, 0x74, 0x82 },
	{ 0x4e, 0x00, 0x25 }, { 0x2d, 0x46, 0x9d }, { 0x52, 0x00, 0x7c }, { 0x34, 0x59, 0xe3 },
	{ 0xaa, 0x17, 0x00 }, { 0x85, 0x79, 0x3d }, { 0xa7, 0x2e, 0x24 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 25 */
	{ 0x50, 0x00, 0x7e }, { 0x2d, 0x45, 0x9d }, { 0x50, 0x00, 0x7e }, { 0x30, 0x57, 0xde },
	{ 0x23, 0x16, 0x29 }, { 0x05, 0x61, 0x49 }, { 0x23, 0x13, 0x26 }, { 0x04, 0x75, 0x87 },
	{ 0x50, 0x00, 0x7e }, { 0x28, 0x44, 0x96 }, { 0x50, 0x00, 0x7e }, { 0x31, 0x59, 0xdf },
	{ 0xac, 0x28, 0x33 }, { 0x85, 0x79, 0x3c }, { 0xa7, 0x2d, 0x23 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 26 */
	{ 0x4f, 0x62, 0x00 }, { 0x3d, 0x71, 0x1f }, { 0x60, 0x2a, 0x07 }, { 0x43, 0x88, 0x69 },
	{ 0x13, 0x42, 0x19 }, { 0x05, 0x63, 0x46 }, { 0x24, 0x16, 0x27 }, { 0x07, 0x72, 0x91 },
	{ 0x3c, 0x2b, 0x5f }, { 0x2a, 0x45, 0x92 }, { 0x4f, 0x00, 0x82 }, { 0x36, 0x57, 0xe9 },
	{ 0x92, 0x5a, 0x0b }, { 0x87, 0x78, 0x45 }, { 0xa7, 0x2c, 0x2a }, { 0x8b, 0x8b, 0x8c },
/* Composite lo-res, colour reg = 27 */
	{ 0x8b, 0x8b, 0x8b }, { 0x89, 0x78, 0x47 }, { 0xa8, 0x2a, 0x2d }, { 0x8b, 0x8b, 0x8b },
	{ 0x08, 0x71, 0x93 }, { 0x02, 0x62, 0x4a }, { 0x26, 0x16, 0x2a }, { 0x06, 0x73, 0x87 },
	{ 0x35, 0x58, 0xe6 }, { 0x2f, 0x45, 0x9e }, { 0x50, 0x00, 0x78 }, { 0x2f, 0x59, 0xe0 },
	{ 0x8b, 0x8b, 0x8b }, { 0x87, 0x7a, 0x46 }, { 0xaa, 0x29, 0x30 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 28 */
	{ 0x25, 0x25, 0x25 }, { 0x08, 0x5f, 0x4b }, { 0x2b, 0x15, 0x25 }, { 0x09, 0x71, 0x88 },
	{ 0x1e, 0x23, 0x26 }, { 0x04, 0x62, 0x47 }, { 0x21, 0x19, 0x28 }, { 0x06, 0x74, 0x88 },
	{ 0x48, 0x0b, 0x70 }, { 0x26, 0x42, 0x95 }, { 0x52, 0x00, 0x7c }, { 0x34, 0x58, 0xe6 },
	{ 0xa1, 0x37, 0x1c }, { 0x85, 0x78, 0x3e }, { 0xa6, 0x2e, 0x23 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 29 */
	{ 0x5e, 0x44, 0xd3 }, { 0x45, 0x61, 0x73 }, { 0x6c, 0x18, 0x53 }, { 0x4c, 0x72, 0xbf },
	{ 0x14, 0x45, 0xa3 }, { 0x04, 0x61, 0x49 }, { 0x22, 0x15, 0x25 }, { 0x06, 0x72, 0x8d },
	{ 0x41, 0x2d, 0xf6 }, { 0x27, 0x43, 0x96 }, { 0x51, 0x00, 0x7c }, { 0x34, 0x58, 0xe4 },
	{ 0x9b, 0x5a, 0xa5 }, { 0x85, 0x78, 0x3d }, { 0xa6, 0x2e, 0x23 }, { 0x8c, 0x8c, 0x8c },
/* Composite lo-res, colour reg = 2A */
	{ 0x5c, 0xae, 0x2a }, { 0x58, 0x91, 0x00 }, { 0x7b, 0x41, 0x00 }, { 0x5e, 0xa0, 0x36 },
	{ 0x06, 0x78, 0x86 }, { 0x03, 0x62, 0x49 }, { 0x25, 0x14, 0x28 }, { 0x03, 0x74, 0x82 },
	{ 0x25, 0x5b, 0xcc }, { 0x2a, 0x43, 0x97 }, { 0x52, 0x00, 0x79 }, { 0x31, 0x5b, 0xe0 },
	{ 0x7e, 0x88, 0x7b }, { 0x86, 0x7b, 0x3e }, { 0xa7, 0x2c, 0x22 }, { 0x89, 0x89, 0x89 },
/* Composite lo-res, colour reg = 2B */
	{ 0x58, 0xbb, 0x98 }, { 0x5a, 0x8c, 0x0a }, { 0x7f, 0x3b, 0x02 }, { 0x60, 0x9a, 0x4b },
	{ 0x03, 0x96, 0xce }, { 0x04, 0x61, 0x4a }, { 0x23, 0x14, 0x24 }, { 0x04, 0x75, 0x86 },
	{ 0x23, 0x76, 0xfe }, { 0x28, 0x43, 0x95 }, { 0x51, 0x00, 0x7b }, { 0x30, 0x59, 0xdb },
	{ 0x80, 0xab, 0xd2 }, { 0x85, 0x7a, 0x3d }, { 0xa8, 0x2e, 0x26 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 2C */
	{ 0xa9, 0x46, 0x66 }, { 0x7f, 0x6f, 0x37 }, { 0xa9, 0x27, 0x27 }, { 0x87, 0x82, 0x7f },
	{ 0x1e, 0x31, 0x5c }, { 0x04, 0x63, 0x44 }, { 0x23, 0x16, 0x2a }, { 0x08, 0x71, 0x92 },
	{ 0x4e, 0x1a, 0xb0 }, { 0x27, 0x46, 0x92 }, { 0x50, 0x00, 0x80 }, { 0x33, 0x56, 0xe7 },
	{ 0xa3, 0x4a, 0x58 }, { 0x87, 0x78, 0x46 }, { 0xab, 0x29, 0x34 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 2D */
	{ 0xa8, 0x53, 0xd8 }, { 0x82, 0x6c, 0x5f }, { 0xac, 0x25, 0x3f }, { 0x8d, 0x7f, 0xa8 },
	{ 0x21, 0x46, 0xb9 }, { 0x04, 0x61, 0x4d }, { 0x24, 0x13, 0x23 }, { 0x04, 0x75, 0x87 },
	{ 0x4c, 0x2e, 0xfe }, { 0x2a, 0x45, 0x99 }, { 0x52, 0x00, 0x78 }, { 0x32, 0x5a, 0xde },
	{ 0xa8, 0x53, 0xd8 }, { 0x84, 0x7c, 0x3d }, { 0xa7, 0x2d, 0x22 }, { 0x8b, 0x8b, 0x8b },
/* Composite lo-res, colour reg = 2E */
	{ 0xa4, 0xba, 0x2e }, { 0x8e, 0x9f, 0x00 }, { 0xbf, 0x4e, 0x00 }, { 0xa5, 0xae, 0x2d },
	{ 0x13, 0x71, 0xa6 }, { 0x03, 0x62, 0x4a }, { 0x24, 0x14, 0x28 }, { 0x05, 0x74, 0x83 },
	{ 0x32, 0x5d, 0xe0 }, { 0x2e, 0x46, 0x9c }, { 0x51, 0x00, 0x7c }, { 0x2f, 0x59, 0xe0 },
	{ 0x8a, 0x8d, 0x94 }, { 0x86, 0x7b, 0x40 }, { 0xa8, 0x2c, 0x22 }, { 0x8a, 0x8a, 0x8a },
/* Composite lo-res, colour reg = 2F */
	{ 0xe4, 0xe4, 0xe4 }, { 0xdd, 0xa6, 0x0a }, { 0xf9, 0x53, 0x04 }, { 0xea, 0xae, 0x54 },
	{ 0x08, 0xa2, 0xfc }, { 0x03, 0x62, 0x48 }, { 0x24, 0x14, 0x28 }, { 0x05, 0x74, 0x84 },
	{ 0x27, 0x90, 0xff }, { 0x2a, 0x43, 0x95 }, { 0x52, 0x00, 0x79 }, { 0x34, 0x5a, 0xe3 },
	{ 0x85, 0xbb, 0xff }, { 0x85, 0x7a, 0x3d }, { 0xa7, 0x2c, 0x23 }, { 0x8a, 0x8a, 0x8a },
/* Composite lo-res, colour reg = 30 */
	{ 0x00, 0x00, 0x00 }, { 0x00, 0x63, 0xfe }, { 0x00, 0x1d, 0xe9 }, { 0x00, 0x81, 0xff },
	{ 0x7e, 0x16, 0x00 }, { 0x5b, 0xb9, 0xa5 }, { 0x79, 0x6a, 0x79 }, { 0x59, 0xce, 0xdc },
	{ 0xa8, 0x05, 0x00 }, { 0x84, 0x9e, 0xf3 }, { 0xaa, 0x54, 0xd3 }, { 0x8c, 0xb1, 0xff },
	{ 0xfb, 0x28, 0x00 }, { 0xde, 0xd2, 0x94 }, { 0xfc, 0x85, 0x7b }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 31 */
	{ 0x07, 0x00, 0x7c }, { 0x00, 0x70, 0xfe }, { 0x0d, 0x20, 0xff }, { 0x04, 0x7f, 0xff },
	{ 0x6f, 0x3b, 0x00 }, { 0x59, 0xbb, 0x9b }, { 0x79, 0x6c, 0x81 }, { 0x5d, 0xcb, 0xe4 },
	{ 0x99, 0x26, 0x29 }, { 0x83, 0x9d, 0xf2 }, { 0xaa, 0x54, 0xd4 }, { 0x88, 0xb0, 0xff },
	{ 0xf4, 0x57, 0x00 }, { 0xdf, 0xd3, 0x9a }, { 0xfe, 0x81, 0x7f }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 32 */
	{ 0x06, 0x55, 0x00 }, { 0x03, 0x99, 0xbe }, { 0x22, 0x46, 0xa4 }, { 0x09, 0xa4, 0xfa },
	{ 0x62, 0x6d, 0x00 }, { 0x59, 0xbb, 0x9b }, { 0x7a, 0x6d, 0x7e }, { 0x5c, 0xc8, 0xe7 },
	{ 0x8a, 0x4f, 0x11 }, { 0x80, 0x9b, 0xea }, { 0xa7, 0x55, 0xda }, { 0x8c, 0xad, 0xff },
	{ 0xdf, 0x7f, 0x00 }, { 0xe2, 0xd1, 0x9c }, { 0xfd, 0x81, 0x87 }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 33 */
	{ 0x03, 0x63, 0x47 }, { 0x06, 0x91, 0xda }, { 0x23, 0x41, 0xbc }, { 0x06, 0xa5, 0xfa },
	{ 0x5d, 0x8a, 0x07 }, { 0x59, 0xbb, 0x9b }, { 0x7c, 0x6e, 0x80 }, { 0x5d, 0xc9, 0xe8 },
	{ 0x86, 0x6b, 0x60 }, { 0x81, 0x9d, 0xea }, { 0xa8, 0x55, 0xd8 }, { 0x8e, 0xae, 0xff },
	{ 0xdf, 0xa4, 0x0c }, { 0xe1, 0xd0, 0x9e }, { 0xfd, 0x81, 0x85 }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 34 */
	{ 0x52, 0x00, 0x14 }, { 0x20, 0x7b, 0xf7 }, { 0x4e, 0x2f, 0xed }, { 0x22, 0x91, 0xff },
	{ 0x7f, 0x28, 0x00 }, { 0x5a, 0xbb, 0x9a }, { 0x79, 0x6d, 0x7f }, { 0x5e, 0xc8, 0xeb },
	{ 0xac, 0x13, 0x02 }, { 0x7e, 0x9c, 0xea }, { 0xa8, 0x54, 0xd6 }, { 0x8e, 0xae, 0xff },
	{ 0xf5, 0x43, 0x00 }, { 0xdf, 0xcf, 0x9d }, { 0xfd, 0x81, 0x88 }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 35 */
	{ 0x50, 0x00, 0x7b }, { 0x22, 0x76, 0xfe }, { 0x4c, 0x2f, 0xff }, { 0x36, 0x85, 0xff },
	{ 0x7b, 0x3d, 0x00 }, { 0x5b, 0xbc, 0xa1 }, { 0x7b, 0x6c, 0x7f }, { 0x5d, 0xca, 0xe8 },
	{ 0xa9, 0x26, 0x3c }, { 0x81, 0x9c, 0xec }, { 0xa7, 0x54, 0xdc }, { 0x8b, 0xad, 0xff },
	{ 0xf8, 0x57, 0x03 }, { 0xe1, 0xd4, 0x9e }, { 0xfd, 0x80, 0x82 }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 36 */
	{ 0x4c, 0x61, 0x00 }, { 0x39, 0xa2, 0xaf }, { 0x5b, 0x5a, 0x8c }, { 0x42, 0xb6, 0xf4 },
	{ 0x66, 0x6b, 0x00 }, { 0x5b, 0xba, 0xa4 }, { 0x7a, 0x6c, 0x7e }, { 0x5e, 0xca, 0xe8 },
	{ 0x91, 0x51, 0x1f }, { 0x85, 0x9d, 0xf4 }, { 0xaa, 0x55, 0xd7 }, { 0x88, 0xaf, 0xff },
	{ 0xea, 0x87, 0x00 }, { 0xde, 0xd4, 0x98 }, { 0xfd, 0x7f, 0x81 }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 37 */
	{ 0x8b, 0x8b, 0x8b }, { 0x86, 0xa7, 0xde }, { 0xa6, 0x5a, 0xbf }, { 0x85, 0xbb, 0xff },
	{ 0x5f, 0x9b, 0x51 }, { 0x5a, 0xb9, 0xa7 }, { 0x78, 0x6a, 0x7b }, { 0x5b, 0xce, 0xdb },
	{ 0x8b, 0x7c, 0xae }, { 0x82, 0x9c, 0xf2 }, { 0xaa, 0x54, 0xd3 }, { 0x87, 0xb2, 0xff },
	{ 0xe7, 0xb0, 0x54 }, { 0xdc, 0xd2, 0x95 }, { 0xfc, 0x84, 0x77 }, { 0xe3, 0xe3, 0xe3 },
/* Composite lo-res, colour reg = 38 */
	{ 0x24, 0x24, 0x24 }, { 0x06, 0x91, 0xd8 }, { 0x2a, 0x44, 0xb9 }, { 0x0f, 0x9e, 0xfe },
	{ 0x7a, 0x49, 0x00 }, { 0x58, 0xbc, 0x98 }, { 0x7a, 0x6d, 0x7f }, { 0x5e, 0xc8, 0xeb },
	{ 0xa3, 0x36, 0x3a }, { 0x7f, 0x9c, 0xec }, { 0xa8, 0x54, 0xd7 }, { 0x8a, 0xad, 0xff },
	{ 0xf7, 0x64, 0x00 }, { 0xe1, 0xd1, 0x9c }, { 0xfd, 0x7f, 0x8b }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 39 */
	{ 0x62, 0x46, 0xd3 }, { 0x3d, 0x93, 0xf2 }, { 0x6b, 0x46, 0xeb }, { 0x4f, 0xa0, 0xff },
	{ 0x6f, 0x69, 0x6b }, { 0x58, 0xbb, 0x9b }, { 0x7b, 0x6e, 0x80 }, { 0x5e, 0xc8, 0xec },
	{ 0x9d, 0x53, 0xbd }, { 0x81, 0x9d, 0xf0 }, { 0xa8, 0x54, 0xd8 }, { 0x86, 0xb1, 0xff },
	{ 0xf4, 0x85, 0x5e }, { 0xdf, 0xd1, 0x9f }, { 0xfe, 0x7f, 0x88 }, { 0xe3, 0xe3, 0xe3 },
/* Composite lo-res, colour reg = 3A */
	{ 0x5a, 0xad, 0x2d }, { 0x58, 0xc1, 0x81 }, { 0x77, 0x74, 0x68 }, { 0x58, 0xcf, 0xd1 },
	{ 0x5d, 0xa0, 0x4d }, { 0x59, 0xbb, 0x9b }, { 0x7c, 0x6d, 0x7f }, { 0x5e, 0xc9, 0xeb },
	{ 0x83, 0x7d, 0x9e }, { 0x7f, 0x9c, 0xec }, { 0xa9, 0x54, 0xd6 }, { 0x8c, 0xae, 0xff },
	{ 0xde, 0xae, 0x4b }, { 0xdc, 0xd0, 0x98 }, { 0xfc, 0x81, 0x8a }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 3B */
	{ 0x59, 0xbc, 0x9c }, { 0x59, 0xbc, 0x9c }, { 0x7a, 0x6a, 0x79 }, { 0x59, 0xce, 0xd9 },
	{ 0x59, 0xbc, 0x9c }, { 0x59, 0xbc, 0x9c }, { 0x7b, 0x6b, 0x80 }, { 0x59, 0xcc, 0xd9 },
	{ 0x81, 0x9b, 0xec }, { 0x81, 0x9b, 0xec }, { 0xa9, 0x54, 0xd4 }, { 0x8e, 0xb2, 0xff },
	{ 0xdf, 0xcf, 0x9b }, { 0xdf, 0xcf, 0x9b }, { 0xfd, 0x85, 0x79 }, { 0xe3, 0xe3, 0xe3 },
/* Composite lo-res, colour reg = 3C */
	{ 0xaa, 0x46, 0x6a }, { 0x7a, 0xa3, 0xc7 }, { 0xa7, 0x58, 0xba }, { 0x84, 0xb5, 0xf8 },
	{ 0x78, 0x5b, 0x23 }, { 0x59, 0xbc, 0x9c }, { 0x7a, 0x6e, 0x81 }, { 0x5f, 0xc9, 0xeb },
	{ 0xa3, 0x44, 0x71 }, { 0x80, 0x9d, 0xec }, { 0xa8, 0x54, 0xd7 }, { 0x8a, 0xae, 0xff },
	{ 0xf8, 0x74, 0x1a }, { 0xdf, 0xd1, 0x9e }, { 0xfe, 0x81, 0x8b }, { 0xe3, 0xe3, 0xe3 },
/* Composite lo-res, colour reg = 3D */
	{ 0xaa, 0x53, 0xd1 }, { 0x80, 0x9c, 0xec }, { 0xaa, 0x53, 0xd1 }, { 0x88, 0xad, 0xff },
	{ 0x7a, 0x6b, 0x7e }, { 0x58, 0xbb, 0x9d }, { 0x7a, 0x6d, 0x81 }, { 0x5f, 0xc9, 0xe5 },
	{ 0xaa, 0x53, 0xd1 }, { 0x84, 0x9d, 0xf2 }, { 0xaa, 0x53, 0xd1 }, { 0x88, 0xad, 0xff },
	{ 0xfd, 0x85, 0x78 }, { 0xe0, 0xd2, 0x9e }, { 0xfe, 0x80, 0x87 }, { 0xe3, 0xe3, 0xe3 },
/* Composite lo-res, colour reg = 3E */
	{ 0xa1, 0xba, 0x2f }, { 0x90, 0xce, 0x70 }, { 0xb4, 0x80, 0x4b }, { 0x9d, 0xe0, 0xba },
	{ 0x6a, 0x9f, 0x68 }, { 0x5a, 0xba, 0x9f }, { 0x7a, 0x6b, 0x7a }, { 0x5a, 0xce, 0xdb },
	{ 0x91, 0x83, 0xae }, { 0x80, 0x9b, 0xef }, { 0xaa, 0x54, 0xd2 }, { 0x8c, 0xb1, 0xff },
	{ 0xeb, 0xb3, 0x59 }, { 0xdd, 0xd3, 0x94 }, { 0xfc, 0x85, 0x79 }, { 0xe4, 0xe4, 0xe4 },
/* Composite lo-res, colour reg = 3F */
	{ 0xe4, 0xe4, 0xe4 }, { 0xdd, 0xd2, 0x93 }, { 0xfc, 0x85, 0x7a }, { 0xe4, 0xe4, 0xe4 },
	{ 0x59, 0xcc, 0xda }, { 0x59, 0xbb, 0x9c }, { 0x7b, 0x6d, 0x7f }, { 0x5c, 0xca, 0xe5 },
	{ 0x87, 0xb3, 0xff }, { 0x7f, 0x9a, 0xea }, { 0xa8, 0x54, 0xd4 }, { 0x8c, 0xb0, 0xff },
	{ 0xe4, 0xe4, 0xe4 }, { 0xdf, 0xd1, 0x98 }, { 0xfd, 0x84, 0x7d }, { 0xe4, 0xe4, 0xe4 },
};


static MC6845_UPDATE_ROW( cga_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	if ( cga->m_update_row )
	{
		cga->m_update_row( device, bitmap, cliprect, ma, ra, y, x_count, cursor_x, param );
	}
}


static MC6845_INTERFACE( mc6845_cga_intf )
{
	false,              /* show border area */
	8,                  /* numbers of pixels per video memory address */
	NULL,               /* begin_update */
	cga_update_row,     /* update_row */
	NULL,               /* end_update */
	DEVCB_NULL,             /* on_de_changed */
	DEVCB_NULL,             /* on_cur_changed */
	DEVCB_DEVICE_LINE_MEMBER(DEVICE_SELF_OWNER, isa8_cga_device, hsync_changed),    /* on_hsync_changed */
	DEVCB_DEVICE_LINE_MEMBER(DEVICE_SELF_OWNER, isa8_cga_device, vsync_changed),    /* on_vsync_changed */
	NULL
};

#define CGA_HCLK (XTAL_14_31818MHz/8)
#define CGA_LCLK (XTAL_14_31818MHz/16)


static MACHINE_CONFIG_FRAGMENT( cga )
	MCFG_SCREEN_ADD(CGA_SCREEN_NAME, RASTER)
	MCFG_SCREEN_RAW_PARAMS(XTAL_14_31818MHz,912,0,640,262,0,200)
	MCFG_SCREEN_UPDATE_DEVICE( DEVICE_SELF, isa8_cga_device, screen_update )

	MCFG_PALETTE_LENGTH(/* CGA_PALETTE_SETS * 16*/ 65536 )

	MCFG_MC6845_ADD(CGA_MC6845_NAME, MC6845, CGA_SCREEN_NAME, XTAL_14_31818MHz/8, mc6845_cga_intf)
MACHINE_CONFIG_END


ROM_START( cga )
	/* IBM 1501981(CGA) and 1501985(MDA) Character rom */
	ROM_REGION(0x2000,"gfx1", 0)
	ROM_LOAD("5788005.u33", 0x00000, 0x2000, CRC(0bf56d70) SHA1(c2a8b10808bf51a3c123ba3eb1e9dd608231916f)) /* "AMI 8412PI // 5788005 // (C) IBM CORP. 1981 // KOREA" */
ROM_END


//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

const device_type ISA8_CGA = &device_creator<isa8_cga_device>;

//-------------------------------------------------
//  machine_config_additions - device-specific
//  machine configurations
//-------------------------------------------------

machine_config_constructor isa8_cga_device::device_mconfig_additions() const
{
	return MACHINE_CONFIG_NAME( cga );
}

ioport_constructor isa8_cga_device::device_input_ports() const
{
	return INPUT_PORTS_NAME( cga );
}

//-------------------------------------------------
//  rom_region - device-specific ROM region
//-------------------------------------------------

const rom_entry *isa8_cga_device::device_rom_region() const
{
	return ROM_NAME( cga );
}


//-------------------------------------------------
//  isa8_cga_device - constructor
//-------------------------------------------------

isa8_cga_device::isa8_cga_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		device_t(mconfig, ISA8_CGA, "IBM Color/Graphics Monitor Adapter", tag, owner, clock, "cga", __FILE__),
		device_isa8_card_interface(mconfig, *this),
		m_cga_config(*this, "cga_config"),
		m_vram_size( 0x4000 )
{
	m_chr_gen_offset[0] = m_chr_gen_offset[2] = 0x1800;
	m_chr_gen_offset[1] = m_chr_gen_offset[3] = 0x1000;
	m_font_selection_mask = 0x01;
	m_start_offset = 0;
}

isa8_cga_device::isa8_cga_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source) :
		device_t(mconfig, type, name, tag, owner, clock, shortname, source),
		device_isa8_card_interface(mconfig, *this),
		m_cga_config(*this, "cga_config"),
		m_vram_size( 0x4000 )
{
	m_chr_gen_offset[0] = m_chr_gen_offset[2] = 0x1800;
	m_chr_gen_offset[1] = m_chr_gen_offset[3] = 0x1000;
	m_font_selection_mask = 0x01;
	m_start_offset = 0;
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void isa8_cga_device::device_start()
{
	set_isa_device();
	m_vram = auto_alloc_array(machine(), UINT8, m_vram_size);
	m_update_row = NULL;
	m_isa->install_device(0x3d0, 0x3df, 0, 0, read8_delegate( FUNC(isa8_cga_device::io_read), this ), write8_delegate( FUNC(isa8_cga_device::io_write), this ) );
	m_isa->install_bank(0xb8000, 0xb8000 + MIN(0x8000,m_vram_size) - 1, 0, m_vram_size & 0x4000, "bank_cga", m_vram);
	m_superimpose = false;

	/* Initialise the cga palette */
	int i;

	for ( i = 0; i < CGA_PALETTE_SETS * 16; i++ )
	{
		palette_set_color_rgb( machine(), i, cga_palette[i][0], cga_palette[i][1], cga_palette[i][2] );
	}

	i = 0x8000;
	for ( int r = 0; r < 32; r++ )
	{
		for ( int g = 0; g < 32; g++ )
		{
			for ( int b = 0; b < 32; b++ )
			{
				palette_set_color_rgb( machine(), i, r << 3, g << 3, b << 3 );
				i++;
			}
		}
	}

	astring tempstring;
	m_chr_gen_base = memregion(subtag(tempstring, "gfx1"))->base();
	m_chr_gen = m_chr_gen_base + m_chr_gen_offset[1];
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void isa8_cga_device::device_reset()
{
	m_update_row = NULL;
	m_framecnt = 0;
	m_mode_control = 0;
	m_vsync = 0;
	m_hsync = 0;
	m_color_select = 0;
	memset(m_palette_lut_2bpp, 0, sizeof(m_palette_lut_2bpp));
}

/***************************************************************************

    Methods

***************************************************************************/


const device_type ISA8_CGA_MC1502 = &device_creator<isa8_cga_mc1502_device>;

//-------------------------------------------------
//  isa8_cga_mc1502_device - constructor
//-------------------------------------------------

isa8_cga_mc1502_device::isa8_cga_mc1502_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		isa8_cga_device( mconfig, ISA8_CGA_MC1502, "ISA8_CGA_MC1502", tag, owner, clock, "cga_mc1502", __FILE__)
{
	m_vram_size = 0x8000;
}


ROM_START( mc1502 )
	ROM_REGION(0x2000,"gfx1", 0)
	ROM_LOAD( "symgen.rom", 0x0000, 0x2000, CRC(b2747a52) SHA1(6766d275467672436e91ac2997ac6b77700eba1e))
ROM_END

//-------------------------------------------------
//  rom_region - device-specific ROM region
//-------------------------------------------------

const rom_entry *isa8_cga_mc1502_device::device_rom_region() const
{
	return ROM_NAME( mc1502 );
}

UINT32 isa8_cga_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	mc6845_device *mc6845 = subdevice<mc6845_device>(CGA_MC6845_NAME);

	mc6845->screen_update( screen, bitmap, cliprect);

	/* Check for changes in font dipsetting */
	switch ( CGA_FONT )
	{
	case 0:
		m_chr_gen = m_chr_gen_base + m_chr_gen_offset[0];
		break;
	case 1:
		m_chr_gen = m_chr_gen_base + m_chr_gen_offset[1];
		break;
	case 2:
		m_chr_gen = m_chr_gen_base + m_chr_gen_offset[2];
		break;
	case 3:
		m_chr_gen = m_chr_gen_base + m_chr_gen_offset[3];
		break;
	}
	return 0;
}


const device_type ISA8_CGA_POISK1 = &device_creator<isa8_cga_poisk1_device>;

//-------------------------------------------------
//  isa8_cga_poisk1_device - constructor
//-------------------------------------------------

isa8_cga_poisk1_device::isa8_cga_poisk1_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		isa8_cga_device( mconfig, ISA8_CGA_POISK1, "ISA8_CGA_POISK1", tag, owner, clock, "cga_poisk1", __FILE__)
{
	m_chr_gen_offset[0] = 0x0000;
	m_font_selection_mask = 0;
}

ROM_START( cga_poisk1 )
	ROM_REGION(0x2000,"gfx1", 0)
	ROM_LOAD( "poisk.cga", 0x0000, 0x0800, CRC(f6eb39f0) SHA1(0b788d8d7a8e92cc612d044abcb2523ad964c200))
ROM_END

//-------------------------------------------------
//  rom_region - device-specific ROM region
//-------------------------------------------------

const rom_entry *isa8_cga_poisk1_device::device_rom_region() const
{
	return ROM_NAME( cga_poisk1 );
}


const device_type ISA8_CGA_POISK2 = &device_creator<isa8_cga_poisk2_device>;

//-------------------------------------------------
//  isa8_cga_poisk2_device - constructor
//-------------------------------------------------

isa8_cga_poisk2_device::isa8_cga_poisk2_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		isa8_cga_device( mconfig, ISA8_CGA_POISK2, "ISA8_CGA_POISK2", tag, owner, clock, "cga_poisk2", __FILE__)
{
	m_chr_gen_offset[0] = 0x0000;
	m_chr_gen_offset[1] = 0x0800;
}

ROM_START( cga_poisk2 )
	ROM_REGION(0x2000,"gfx1", 0)
	ROM_LOAD( "p2_ecga.rf4", 0x0000, 0x2000, CRC(d537f665) SHA1(d70f085b9b0cbd53df7c3122fbe7592998ba8fed))
ROM_END

//-------------------------------------------------
//  rom_region - device-specific ROM region
//-------------------------------------------------

const rom_entry *isa8_cga_poisk2_device::device_rom_region() const
{
	return ROM_NAME( cga_poisk2 );
}


/* for superimposing CGA over a different source video (i.e. tetriskr) */
const device_type ISA8_CGA_SUPERIMPOSE = &device_creator<isa8_cga_superimpose_device>;

//-------------------------------------------------
//  isa8_cga_superimpose_device - constructor
//-------------------------------------------------

isa8_cga_superimpose_device::isa8_cga_superimpose_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		isa8_cga_device( mconfig, ISA8_CGA_SUPERIMPOSE, "ISA8_CGA_SUPERIMPOSE", tag, owner, clock, "cga_superimpose", __FILE__)
{
	m_superimpose = true;
}


/***************************************************************************
  Draw text mode with 40x25 characters (default) with high intensity bg.
  The character cell size is 16x8
***************************************************************************/

static MC6845_UPDATE_ROW( cga_text_inten_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_text_inten_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ma + i ) << 1 ) & 0x3fff;
		UINT8 chr = videoram[ offset ];
		UINT8 attr = videoram[ offset +1 ];
		UINT8 data = cga->m_chr_gen[ chr * 8 + ra ];
		UINT16 fg = attr & 0x0F;
		UINT16 bg = attr >> 4;

		if ( i == cursor_x && ( cga->m_framecnt & 0x08 ) )
		{
			data = 0xFF;
		}

		*p = palette[( data & 0x80 ) ? fg : bg]; p++;
		*p = palette[( data & 0x40 ) ? fg : bg]; p++;
		*p = palette[( data & 0x20 ) ? fg : bg]; p++;
		*p = palette[( data & 0x10 ) ? fg : bg]; p++;
		*p = palette[( data & 0x08 ) ? fg : bg]; p++;
		*p = palette[( data & 0x04 ) ? fg : bg]; p++;
		*p = palette[( data & 0x02 ) ? fg : bg]; p++;
		*p = palette[( data & 0x01 ) ? fg : bg]; p++;
	}
}


/***************************************************************************
  Draw text mode with 40x25 characters (default) with high intensity bg.
  The character cell size is 16x8. Composite monitor, greyscale.
***************************************************************************/

static MC6845_UPDATE_ROW( cga_text_inten_comp_grey_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_text_inten_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ma + i ) << 1 ) & 0x3fff;
		UINT8 chr = videoram[ offset ];
		UINT8 attr = videoram[ offset +1 ];
		UINT8 data = cga->m_chr_gen[ chr * 8 + ra ];
		UINT16 fg = 0x10 + ( attr & 0x0F );
		UINT16 bg = 0x10 + ( ( attr >> 4 ) & 0x07 );

		if ( i == cursor_x && ( cga->m_framecnt & 0x08 ) )
		{
			data = 0xFF;
		}

		*p = palette[( data & 0x80 ) ? fg : bg]; p++;
		*p = palette[( data & 0x40 ) ? fg : bg]; p++;
		*p = palette[( data & 0x20 ) ? fg : bg]; p++;
		*p = palette[( data & 0x10 ) ? fg : bg]; p++;
		*p = palette[( data & 0x08 ) ? fg : bg]; p++;
		*p = palette[( data & 0x04 ) ? fg : bg]; p++;
		*p = palette[( data & 0x02 ) ? fg : bg]; p++;
		*p = palette[( data & 0x01 ) ? fg : bg]; p++;
	}
}

/***************************************************************************
  Draw text mode with 40x25 characters (default) with high intensity bg.
  The character cell size is 16x8
***************************************************************************/

static MC6845_UPDATE_ROW( cga_text_inten_alt_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_text_inten_alt_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ma + i ) << 1 ) & 0x3fff;
		UINT8 chr = videoram[ offset ];
		UINT8 attr = videoram[ offset +1 ];
		UINT8 data = cga->m_chr_gen[ chr * 8 + ra ];
		UINT16 fg = attr & 0x0F;

		if ( i == cursor_x && ( cga->m_framecnt & 0x08 ) )
		{
			data = 0xFF;
		}

		*p = palette[( data & 0x80 ) ? fg : 0]; p++;
		*p = palette[( data & 0x40 ) ? fg : 0]; p++;
		*p = palette[( data & 0x20 ) ? fg : 0]; p++;
		*p = palette[( data & 0x10 ) ? fg : 0]; p++;
		*p = palette[( data & 0x08 ) ? fg : 0]; p++;
		*p = palette[( data & 0x04 ) ? fg : 0]; p++;
		*p = palette[( data & 0x02 ) ? fg : 0]; p++;
		*p = palette[( data & 0x01 ) ? fg : 0]; p++;
	}
}


/***************************************************************************
  Draw text mode with 40x25 characters (default) and blinking colors.
  The character cell size is 16x8
***************************************************************************/

static MC6845_UPDATE_ROW( cga_text_blink_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_text_blink_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ma + i ) << 1 ) & 0x3fff;
		UINT8 chr = videoram[ offset ];
		UINT8 attr = videoram[ offset +1 ];
		UINT8 data = cga->m_chr_gen[ chr * 8 + ra ];
		UINT16 fg = attr & 0x0F;
		UINT16 bg = (attr >> 4) & 0x07;

		if ( i == cursor_x )
		{
			if ( cga->m_framecnt & 0x08 )
			{
				data = 0xFF;
			}
		}
		else
		{
			if ( ( attr & 0x80 ) && ( cga->m_framecnt & 0x10 ) )
			{
				data = 0x00;
			}
		}

		*p = palette[( data & 0x80 ) ? fg : bg]; p++;
		*p = palette[( data & 0x40 ) ? fg : bg]; p++;
		*p = palette[( data & 0x20 ) ? fg : bg]; p++;
		*p = palette[( data & 0x10 ) ? fg : bg]; p++;
		*p = palette[( data & 0x08 ) ? fg : bg]; p++;
		*p = palette[( data & 0x04 ) ? fg : bg]; p++;
		*p = palette[( data & 0x02 ) ? fg : bg]; p++;
		*p = palette[( data & 0x01 ) ? fg : bg]; p++;
	}
}

static MC6845_UPDATE_ROW( cga_text_blink_update_row_si )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_text_blink_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ma + i ) << 1 ) & 0x3fff;
		UINT8 chr = videoram[ offset ];
		UINT8 attr = videoram[ offset +1 ];
		UINT8 data = cga->m_chr_gen[ chr * 8 + ra ];
		UINT16 fg = attr & 0x0F;
		UINT16 bg = (attr >> 4) & 0x07;
		UINT8 xi;

		if ( i == cursor_x )
		{
			if ( cga->m_framecnt & 0x08 )
			{
				data = 0xFF;
			}
		}
		else
		{
			if ( ( attr & 0x80 ) && ( cga->m_framecnt & 0x10 ) )
			{
				data = 0x00;
			}
		}

		for(xi=0;xi<8;xi++)
		{
			UINT8 pen_data, dot;

			dot = (data & (1 << (7-xi)));
			pen_data = dot ? fg : bg;
			if(pen_data || dot)
				*p = palette[pen_data];
			p++;
		}
	}
}

/***************************************************************************
  Draw text mode with 40x25 characters (default) and blinking colors.
  The character cell size is 16x8
***************************************************************************/

static MC6845_UPDATE_ROW( cga_text_blink_alt_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_text_blink_alt_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ma + i ) << 1 ) & 0x3fff;
		UINT8 chr = videoram[ offset ];
		UINT8 attr = videoram[ offset +1 ];
		UINT8 data = cga->m_chr_gen[ chr * 8 + ra ];
		UINT16 fg = attr & 0x07;
		UINT16 bg = 0;

		if ( i == cursor_x )
		{
			if ( cga->m_framecnt & 0x08 )
			{
				data = 0xFF;
			}
		}
		else
		{
			if ( ( attr & 0x80 ) && ( cga->m_framecnt & 0x10 ) )
			{
				data = 0x00;
				bg = ( attr >> 4 ) & 0x07;
			}
		}

		*p = palette[( data & 0x80 ) ? fg : bg]; p++;
		*p = palette[( data & 0x40 ) ? fg : bg]; p++;
		*p = palette[( data & 0x20 ) ? fg : bg]; p++;
		*p = palette[( data & 0x10 ) ? fg : bg]; p++;
		*p = palette[( data & 0x08 ) ? fg : bg]; p++;
		*p = palette[( data & 0x04 ) ? fg : bg]; p++;
		*p = palette[( data & 0x02 ) ? fg : bg]; p++;
		*p = palette[( data & 0x01 ) ? fg : bg]; p++;
	}
}


/* The lo-res (320x200) graphics mode on a colour composite monitor */

static MC6845_UPDATE_ROW( cga_gfx_4bppl_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_gfx_4bppl_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ( ma + i ) << 1 ) & 0x1fff ) | ( ( y & 1 ) << 13 );
		UINT8 data = videoram[ offset ];

		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;

		data = videoram[ offset + 1 ];

		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;
	}
}


/* The hi-res graphics mode on a colour composite monitor
 *
 * The different scaling factors mean that the '160x200' versions of screens
 * are the same size as the normal colour ones.
 */

static const UINT8 yc_lut2[4] = { 0, 182, 71, 255 };

static const UINT8 yc_lut[16][8] =
{
	{ 0, 0, 0, 0, 0, 0, 0, 0 }, /* black */
	{ 0, 0, 0, 0, 1, 1, 1, 1 }, /* blue */
	{ 0, 1, 1, 1, 1, 0, 0, 0 }, /* green */
	{ 0, 0, 1, 1, 1, 1, 0, 0 }, /* cyan */
	{ 1, 1, 0, 0, 0, 0, 1, 1 }, /* red */
	{ 1, 0, 0, 0, 0, 1, 1, 1 }, /* magenta */
	{ 1, 1, 1, 1, 0, 0, 0, 0 }, /* yellow */
	{ 1, 1, 1, 1, 1, 1, 1, 1 }, /* white */
	/* Intensity set */
	{ 2, 2, 2, 2, 2, 2, 2, 2 }, /* black */
	{ 2, 2, 2, 2, 3, 3, 3, 3 }, /* blue */
	{ 2, 3, 3, 3, 3, 2, 2, 2 }, /* green */
	{ 2, 2, 3, 3, 3, 3, 2, 2 }, /* cyan */
	{ 3, 3, 2, 2, 2, 2, 3, 3 }, /* red */
	{ 3, 2, 2, 2, 2, 3, 3, 3 }, /* magenta */
	{ 3, 3, 3, 3, 2, 2, 2, 2 }, /* yellow */
	{ 3, 3, 3, 3, 3, 3, 3, 3 }, /* white */
};

static MC6845_UPDATE_ROW( cga_gfx_4bpph_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_gfx_4bpph_update_row",("\n"));

	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ( ma + i ) << 1 ) & 0x1fff ) | ( ( y & 1 ) << 13 );
		UINT8 data = videoram[ offset ];

		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;

		data = videoram[ offset + 1 ];

		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data >> 4]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;
		*p = palette[data & 0x0F]; p++;
	}
}


/***************************************************************************
  Draw graphics mode with 320x200 pixels (default) with 2 bits/pixel.
  Even scanlines are from CGA_base + 0x0000, odd from CGA_base + 0x2000
  cga fetches 2 byte per mc6845 access.
***************************************************************************/

static MC6845_UPDATE_ROW( cga_gfx_2bpp_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_gfx_2bpp_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ( ma + i ) << 1 ) & 0x1fff ) | ( ( y & 1 ) << 13 );
		UINT8 data = videoram[ offset ];

		*p = palette[cga->m_palette_lut_2bpp[ ( data >> 6 ) & 0x03 ]]; p++;
		*p = palette[cga->m_palette_lut_2bpp[ ( data >> 4 ) & 0x03 ]]; p++;
		*p = palette[cga->m_palette_lut_2bpp[ ( data >> 2 ) & 0x03 ]]; p++;
		*p = palette[cga->m_palette_lut_2bpp[   data        & 0x03 ]]; p++;

		data = videoram[ offset+1 ];

		*p = palette[cga->m_palette_lut_2bpp[ ( data >> 6 ) & 0x03 ]]; p++;
		*p = palette[cga->m_palette_lut_2bpp[ ( data >> 4 ) & 0x03 ]]; p++;
		*p = palette[cga->m_palette_lut_2bpp[ ( data >> 2 ) & 0x03 ]]; p++;
		*p = palette[cga->m_palette_lut_2bpp[   data        & 0x03 ]]; p++;
	}
}



/***************************************************************************
  Draw graphics mode with 640x200 pixels (default).
  The cell size is 1x1 (1 scanline is the real default)
  Even scanlines are from CGA_base + 0x0000, odd from CGA_base + 0x2000
***************************************************************************/

static MC6845_UPDATE_ROW( cga_gfx_1bpp_update_row )
{
	isa8_cga_device *cga  = downcast<isa8_cga_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	UINT8   fg = cga->m_color_select & 0x0F;
	int i;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"cga_gfx_1bpp_update_row",("\n"));
	for ( i = 0; i < x_count; i++ )
	{
		UINT16 offset = ( ( ( ma + i ) << 1 ) & 0x1fff ) | ( ( ra & 1 ) << 13 );
		UINT8 data = videoram[ offset ];

		*p = palette[( data & 0x80 ) ? fg : 0]; p++;
		*p = palette[( data & 0x40 ) ? fg : 0]; p++;
		*p = palette[( data & 0x20 ) ? fg : 0]; p++;
		*p = palette[( data & 0x10 ) ? fg : 0]; p++;
		*p = palette[( data & 0x08 ) ? fg : 0]; p++;
		*p = palette[( data & 0x04 ) ? fg : 0]; p++;
		*p = palette[( data & 0x02 ) ? fg : 0]; p++;
		*p = palette[( data & 0x01 ) ? fg : 0]; p++;

		data = videoram[ offset + 1 ];

		*p = palette[( data & 0x80 ) ? fg : 0]; p++;
		*p = palette[( data & 0x40 ) ? fg : 0]; p++;
		*p = palette[( data & 0x20 ) ? fg : 0]; p++;
		*p = palette[( data & 0x10 ) ? fg : 0]; p++;
		*p = palette[( data & 0x08 ) ? fg : 0]; p++;
		*p = palette[( data & 0x04 ) ? fg : 0]; p++;
		*p = palette[( data & 0x02 ) ? fg : 0]; p++;
		*p = palette[( data & 0x01 ) ? fg : 0]; p++;
	}
}


WRITE_LINE_MEMBER( isa8_cga_device::hsync_changed )
{
	m_hsync = state ? 1 : 0;
}


WRITE_LINE_MEMBER( isa8_cga_device::vsync_changed )
{
	m_vsync = state ? 9 : 0;
	if ( state )
	{
		m_framecnt++;
	}
}


void isa8_cga_device::set_palette_luts(void)
{
	/* Setup 2bpp palette lookup table */
	if ( m_mode_control & 0x10 )
	{
		m_palette_lut_2bpp[0] = 0;
	}
	else
	{
		m_palette_lut_2bpp[0] = m_color_select & 0x0F;
	}
	if ( m_mode_control & 0x04 )
	{
		m_palette_lut_2bpp[1] = ( ( m_color_select & 0x10 ) >> 1 ) | 3;
		m_palette_lut_2bpp[2] = ( ( m_color_select & 0x10 ) >> 1 ) | 4;
		m_palette_lut_2bpp[3] = ( ( m_color_select & 0x10 ) >> 1 ) | 7;
	}
	else
	{
		if ( m_color_select & 0x20 )
		{
			m_palette_lut_2bpp[1] = ( ( m_color_select & 0x10 ) >> 1 ) | 3;
			m_palette_lut_2bpp[2] = ( ( m_color_select & 0x10 ) >> 1 ) | 5;
			m_palette_lut_2bpp[3] = ( ( m_color_select & 0x10 ) >> 1 ) | 7;
		}
		else
		{
			m_palette_lut_2bpp[1] = ( ( m_color_select & 0x10 ) >> 1 ) | 2;
			m_palette_lut_2bpp[2] = ( ( m_color_select & 0x10 ) >> 1 ) | 4;
			m_palette_lut_2bpp[3] = ( ( m_color_select & 0x10 ) >> 1 ) | 6;
		}
	}
	//logerror("2bpp lut set to %d,%d,%d,%d\n", cga.palette_lut_2bpp[0], cga.palette_lut_2bpp[1], cga.palette_lut_2bpp[2], cga.palette_lut_2bpp[3]);
}

/*
 *  rW  CGA mode control register (see #P138)
 *
 *  x x x 0 1 0 0 0 - 320x200, 40x25 text. Colour on RGB and composite monitors.
 *  x x x 0 1 0 0 1 - 640x200, 80x25 text. Colour on RGB and composite monitors.
 *  x x x 0 1 0 1 0 - 320x200 graphics. Colour on RGB and composite monitors.
 *  x x x 0 1 0 1 1 - unknown/invalid.
 *  x x x 0 1 1 0 0 - 320x200, 40x25 text. Colour on RGB, greyscale on composite monitors.
 *  x x x 0 1 1 0 1 - 640x200, 80x25 text. Colour on RGB, greyscale on composite monitors.
 *  x x x 0 1 1 1 0 - 320x200 graphics. Alternative palette on RGB, greyscale on composite monitors.
 *  x x x 0 1 1 1 1 - unknown/invalid.
 *  x x x 1 1 0 0 0 - unknown/invalid.
 *  x x x 1 1 0 0 1 - unknown/invalid.
 *  x x x 1 1 0 1 0 - 160x200/640x200 graphics. 640x200 ?? on RGB monitor, 160x200 on composite monitor.
 *  x x x 1 1 0 1 1 - unknown/invalid.
 *  x x x 1 1 1 0 0 - unknown/invalid.
 *  x x x 1 1 1 0 1 - unknown/invalid.
 *  x x x 1 1 1 1 0 - 640x200 graphics. Colour on black on RGB monitor, monochrome on composite monitor.
 *  x x x 1 1 1 1 1 - unknown/invalid.
 */
void isa8_cga_device::mode_control_w(UINT8 data)
{
	mc6845_device *mc6845 = subdevice<mc6845_device>(CGA_MC6845_NAME);
	UINT8 monitor = CGA_MONITOR;

	m_mode_control = data;

	//logerror("mode set to %02X\n", cga.mode_control & 0x3F );
	switch ( m_mode_control & 0x3F )
	{
	case 0x08: case 0x09: case 0x0C: case 0x0D:
		mc6845->set_hpixels_per_column( 8 );
		if ( monitor == CGA_MONITOR_COMPOSITE )
		{
			if ( m_mode_control & 0x04 )
			{
				/* Composite greyscale */
				m_update_row = cga_text_inten_comp_grey_update_row;
			}
			else
			{
				/* Composite colour */
				m_update_row = cga_text_inten_update_row;
			}
		}
		else
		{
			/* RGB colour */
			m_update_row = cga_text_inten_update_row;
		}
		break;
	case 0x0A: case 0x0B: case 0x2A: case 0x2B:
		mc6845->set_hpixels_per_column( 8 );
		if ( monitor == CGA_MONITOR_COMPOSITE )
		{
			m_update_row = cga_gfx_4bppl_update_row;
		}
		else
		{
			m_update_row = cga_gfx_2bpp_update_row;
		}
		break;
	case 0x0E: case 0x0F: case 0x2E: case 0x2F:
		mc6845->set_hpixels_per_column( 8 );
		m_update_row = cga_gfx_2bpp_update_row;
		break;
	case 0x18: case 0x19: case 0x1C: case 0x1D:
		mc6845->set_hpixels_per_column( 8 );
		m_update_row = cga_text_inten_alt_update_row;
		break;
	case 0x1A: case 0x1B: case 0x3A: case 0x3B:
		mc6845->set_hpixels_per_column( 16 );
		if ( monitor == CGA_MONITOR_COMPOSITE )
		{
			m_update_row = cga_gfx_4bpph_update_row;
		}
		else
		{
			m_update_row = cga_gfx_1bpp_update_row;
		}
		break;
	case 0x1E: case 0x1F: case 0x3E: case 0x3F:
		mc6845->set_hpixels_per_column( 16 );
		m_update_row = cga_gfx_1bpp_update_row;
		break;
	case 0x28: case 0x29: case 0x2C: case 0x2D:
		mc6845->set_hpixels_per_column( 8 );
		if ( monitor == CGA_MONITOR_COMPOSITE )
		{
			if ( m_mode_control & 0x04 )
			{
				/* Composite greyscale */
				m_update_row = m_superimpose ? cga_text_blink_update_row_si : cga_text_blink_update_row;
			}
			else
			{
				/* Composite colour */
				m_update_row = m_superimpose ? cga_text_blink_update_row_si : cga_text_blink_update_row;
			}
		}
		else
		{
			/* RGB colour */
			m_update_row = m_superimpose ? cga_text_blink_update_row_si : cga_text_blink_update_row;
		}
		break;
	case 0x38: case 0x39: case 0x3C: case 0x3D:
		mc6845->set_hpixels_per_column( 8 );
		m_update_row = cga_text_blink_alt_update_row;
		break;
	default:
		m_update_row = NULL;
		break;
	}

	// The lowest bit of the mode register selects, among others, the
	// input clock to the 6845.
	mc6845->set_clock( ( m_mode_control & 1 ) ? CGA_HCLK : CGA_LCLK );

	set_palette_luts();
}



/*
 * Select Plantronics modes
 */
void isa8_cga_device::plantronics_w(UINT8 data)
{
	if ( ( CGA_CHIPSET ) != CGA_CHIPSET_ATI) return;

	data &= 0x70;   /* Only bits 6-4 are used */
	m_plantronics = data;
}



/*************************************************************************
 *
 *      CGA
 *      color graphics adapter
 *
 *************************************************************************/

WRITE8_MEMBER( isa8_cga_device::char_ram_write )
{
	logerror("write char ram %04x %02x\n",offset,data);
	m_chr_gen_base[offset + 0x0000] = data;
	m_chr_gen_base[offset + 0x0800] = data;
	m_chr_gen_base[offset + 0x1000] = data;
	m_chr_gen_base[offset + 0x1800] = data;
}


READ8_MEMBER( isa8_cga_device::char_ram_read )
{
	return m_chr_gen_base[offset];
}


READ8_MEMBER( isa8_cga_device::io_read )
{
	mc6845_device *mc6845 = subdevice<mc6845_device>(CGA_MC6845_NAME);
	UINT8 data = 0xff;

	switch( offset )
	{
		case 0: case 2: case 4: case 6:
			/* return last written mc6845 address value here? */
			break;
		case 1: case 3: case 5: case 7:
			data = mc6845->register_r( space, offset );
			break;
		case 10:
			data = m_vsync | ( ( data & 0x40 ) >> 4 ) | m_hsync;
			break;
		case 0x0f:
			data = m_p3df;
			break;
	}
	return data;
}



WRITE8_MEMBER( isa8_cga_device::io_write )
{
	mc6845_device *mc6845 = subdevice<mc6845_device>(CGA_MC6845_NAME);

	switch(offset) {
	case 0: case 2: case 4: case 6:
		mc6845->address_w( space, offset, data );
		break;
	case 1: case 3: case 5: case 7:
		mc6845->register_w( space, offset, data );
		break;
	case 8:
		mode_control_w(data);
		break;
	case 9:
		m_color_select = data;
		set_palette_luts();
		break;
	case 0x0d:
		plantronics_w(data);
		break;
	case 0x0f:
		// Not sure if some all CGA cards have ability to upload char definition
		// The original CGA card had a char rom
		// TODO: This should be moved to card implementations that actually had this feature
		m_p3df = data;
		if (data & 1) {
			address_space &space_prg = machine().firstcpu->space(AS_PROGRAM);

			space_prg.install_readwrite_handler(0xb8000, 0xb87ff, read8_delegate( FUNC(isa8_cga_device::char_ram_read), this), write8_delegate(FUNC(isa8_cga_device::char_ram_write), this) );
		} else {
			m_isa->install_bank(0xb8000, 0xb8000 + MIN(0x8000,m_vram_size) - 1, 0, m_vram_size & 0x4000, "bank_cga", m_vram);
		}
		break;

	}
}



/* Old plantronics rendering code, leaving it uncommented until we have re-implemented it */

//
// From choosevideomode:
//
//      /* Plantronics high-res */
//      if ((cga.mode_control & 2) && (cga.plantronics & 0x20))
//          proc = cga_pgfx_2bpp;
//      /* Plantronics low-res */
//      if ((cga.mode_control & 2) && (cga.plantronics & 0x10))
//          proc = cga_pgfx_4bpp;
//

//INLINE void pgfx_plot_unit_4bpp(bitmap_ind16 &bitmap,
//                           int x, int y, int offs)
//{
//  int color, values[2];
//  int i;
//
//  if (cga.plantronics & 0x40)
//  {
//      values[0] = videoram[offs | 0x4000];
//      values[1] = videoram[offs];
//  }
//  else
//  {
//      values[0] = videoram[offs];
//      values[1] = videoram[offs | 0x4000];
//  }
//  for (i=3; i>=0; i--)
//  {
//      color = ((values[0] & 0x3) << 1) |
//          ((values[1] & 2)   >> 1) |
//          ((values[1] & 1)   << 3);
//      bitmap.pix16(y, x+i) = Machine->pens[color];
//      values[0]>>=2;
//      values[1]>>=2;
//  }
//}
//
//
//
///***************************************************************************
//  Draw graphics mode with 640x200 pixels (default) with 2 bits/pixel.
//  Even scanlines are from CGA_base + 0x0000, odd from CGA_base + 0x2000
//  Second plane at CGA_base + 0x4000 / 0x6000
//***************************************************************************/
//
//static void cga_pgfx_4bpp(bitmap_ind16 &bitmap, struct mscrtc6845 *crtc)
//{
//  int i, sx, sy, sh;
//  int offs = mscrtc6845_get_start(crtc)*2;
//  int lines = mscrtc6845_get_char_lines(crtc);
//  int height = mscrtc6845_get_char_height(crtc);
//  int columns = mscrtc6845_get_char_columns(crtc)*2;
//
//  for (sy=0; sy<lines; sy++,offs=(offs+columns)&0x1fff)
//  {
//      for (sh=0; sh<height; sh++, offs|=0x2000)
//      {
//          // char line 0 used as a12 line in graphic mode
//          if (!(sh & 1))
//          {
//              for (i=offs, sx=0; sx<columns; sx++, i=(i+1)&0x1fff)
//              {
//                  pgfx_plot_unit_4bpp(bitmap, sx*4, sy*height+sh, i);
//              }
//          }
//          else
//          {
//              for (i=offs|0x2000, sx=0; sx<columns; sx++, i=((i+1)&0x1fff)|0x2000)
//              {
//                  pgfx_plot_unit_4bpp(bitmap, sx*4, sy*height+sh, i);
//              }
//          }
//      }
//  }
//}
//
//
//
//INLINE void pgfx_plot_unit_2bpp(bitmap_ind16 &bitmap,
//                   int x, int y, const UINT16 *palette, int offs)
//{
//  int i;
//  UINT8 bmap[2], values[2];
//  UINT16 *dest;
//
//  if (cga.plantronics & 0x40)
//  {
//      values[0] = videoram[offs];
//      values[1] = videoram[offs | 0x4000];
//  }
//  else
//  {
//      values[0] = videoram[offs | 0x4000];
//      values[1] = videoram[offs];
//  }
//  bmap[0] = bmap[1] = 0;
//  for (i=3; i>=0; i--)
//  {
//      bmap[0] = bmap[0] << 1; if (values[0] & 0x80) bmap[0] |= 1;
//      bmap[0] = bmap[0] << 1; if (values[1] & 0x80) bmap[0] |= 1;
//      bmap[1] = bmap[1] << 1; if (values[0] & 0x08) bmap[1] |= 1;
//      bmap[1] = bmap[1] << 1; if (values[1] & 0x08) bmap[1] |= 1;
//      values[0] = values[0] << 1;
//      values[1] = values[1] << 1;
//  }
//
//  dest = &bitmap.pix16(y, x);
//  *(dest++) = palette[(bmap[0] >> 6) & 0x03];
//  *(dest++) = palette[(bmap[0] >> 4) & 0x03];
//  *(dest++) = palette[(bmap[0] >> 2) & 0x03];
//  *(dest++) = palette[(bmap[0] >> 0) & 0x03];
//  *(dest++) = palette[(bmap[1] >> 6) & 0x03];
//  *(dest++) = palette[(bmap[1] >> 4) & 0x03];
//  *(dest++) = palette[(bmap[1] >> 2) & 0x03];
//  *(dest++) = palette[(bmap[1] >> 0) & 0x03];
//}
//
//
//
///***************************************************************************
//  Draw graphics mode with 320x200 pixels (default) with 2 bits/pixel.
//  Even scanlines are from CGA_base + 0x0000, odd from CGA_base + 0x2000
//  cga fetches 2 byte per mscrtc6845 access (not modeled here)!
//***************************************************************************/
//
//static void cga_pgfx_2bpp(bitmap_ind16 &bitmap, struct mscrtc6845 *crtc)
//{
//  int i, sx, sy, sh;
//  int offs = mscrtc6845_get_start(crtc)*2;
//  int lines = mscrtc6845_get_char_lines(crtc);
//  int height = mscrtc6845_get_char_height(crtc);
//  int columns = mscrtc6845_get_char_columns(crtc)*2;
//  int colorset = cga.color_select & 0x3F;
//  const UINT16 *palette;
//
//  /* Most chipsets use bit 2 of the mode control register to
//   * access a third palette. But not consistently. */
//  pc_cga_check_palette();
//  switch(CGA_CHIPSET)
//  {
//      /* The IBM Professional Graphics Controller behaves like
//       * the PC1512, btw. */
//      case CGA_CHIPSET_PC1512:
//      if ((colorset < 32) && (cga.mode_control & 4)) colorset += 64;
//      break;
//
//      case CGA_CHIPSET_IBM:
//      case CGA_CHIPSET_PC200:
//      case CGA_CHIPSET_ATI:
//      case CGA_CHIPSET_PARADISE:
//      if (cga.mode_control & 4) colorset = (colorset & 0x1F) + 64;
//      break;
//  }
//
//
//  /* The fact that our palette is located in cga_colortable is a vestigial
//   * aspect from when we were doing that ugly trick where drawgfx() would
//   * handle graphics drawing.  Truthfully, we should probably be using
//   * palette_set_color_rgb() here and not doing the palette lookup in the loop
//   */
//  palette = &cga_colortable[256*2 + 16*2] + colorset * 4;
//
//  for (sy=0; sy<lines; sy++,offs=(offs+columns)&0x1fff) {
//
//      for (sh=0; sh<height; sh++)
//      {
//          if (!(sh&1)) { // char line 0 used as a12 line in graphic mode
//              for (i=offs, sx=0; sx<columns; sx++, i=(i+1)&0x1fff)
//              {
//                  pgfx_plot_unit_2bpp(bitmap, sx*8, sy*height+sh, palette, i);
//              }
//          }
//          else
//          {
//              for (i=offs|0x2000, sx=0; sx<columns; sx++, i=((i+1)&0x1fff)|0x2000)
//              {
//                  pgfx_plot_unit_2bpp(bitmap, sx*8, sy*height+sh, palette, i);
//              }
//          }
//      }
//  }
//}


static MC6845_UPDATE_ROW( pc1512_gfx_4bpp_update_row )
{
	isa8_cga_pc1512_device *cga  = downcast<isa8_cga_pc1512_device *>(device->owner());
	UINT8 *videoram = cga->m_vram + cga->m_start_offset;
	UINT32  *p = &bitmap.pix32(y);
	const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
	UINT16  offset_base = ra << 13;
	int j;
	running_machine &machine = device->machine();

	if ( y == 0 ) CGA_LOG(1,"pc1512_gfx_4bpp_update_row",("\n"));
	for ( j = 0; j < x_count; j++ )
	{
		UINT16 offset = offset_base | ( ( ma + j ) & 0x1FFF );
		UINT16 i = ( cga->m_color_select & 8 ) ? videoram[ isa8_cga_pc1512_device::vram_offset[3] | offset ] << 3 : 0;
		UINT16 r = ( cga->m_color_select & 4 ) ? videoram[ isa8_cga_pc1512_device::vram_offset[2] | offset ] << 2 : 0;
		UINT16 g = ( cga->m_color_select & 2 ) ? videoram[ isa8_cga_pc1512_device::vram_offset[1] | offset ] << 1 : 0;
		UINT16 b = ( cga->m_color_select & 1 ) ? videoram[ isa8_cga_pc1512_device::vram_offset[0] | offset ]      : 0;

		*p = palette[( ( i & 0x400 ) | ( r & 0x200 ) | ( g & 0x100 ) | ( b & 0x80 ) ) >> 7]; p++;
		*p = palette[( ( i & 0x200 ) | ( r & 0x100 ) | ( g & 0x080 ) | ( b & 0x40 ) ) >> 6]; p++;
		*p = palette[( ( i & 0x100 ) | ( r & 0x080 ) | ( g & 0x040 ) | ( b & 0x20 ) ) >> 5]; p++;
		*p = palette[( ( i & 0x080 ) | ( r & 0x040 ) | ( g & 0x020 ) | ( b & 0x10 ) ) >> 4]; p++;
		*p = palette[( ( i & 0x040 ) | ( r & 0x020 ) | ( g & 0x010 ) | ( b & 0x08 ) ) >> 3]; p++;
		*p = palette[( ( i & 0x020 ) | ( r & 0x010 ) | ( g & 0x008 ) | ( b & 0x04 ) ) >> 2]; p++;
		*p = palette[( ( i & 0x010 ) | ( r & 0x008 ) | ( g & 0x004 ) | ( b & 0x02 ) ) >> 1]; p++;
		*p = palette[  ( i & 0x008 ) | ( r & 0x004 ) | ( g & 0x002 ) | ( b & 0x01 )       ]; p++;
	}
}


WRITE8_MEMBER( isa8_cga_pc1512_device::io_write )
{
	mc6845_device *mc6845 = subdevice<mc6845_device>(CGA_MC6845_NAME);

	switch (offset)
	{
	case 0: case 2: case 4: case 6:
		data &= 0x1F;
		mc6845->address_w( space, offset, data );
		m_mc6845_address = data;
		break;

	case 1: case 3: case 5: case 7:
		if ( ! m_mc6845_locked_register[m_mc6845_address] )
		{
			mc6845->register_w( space, offset, data );
			if ( isa8_cga_pc1512_device::mc6845_writeonce_register[m_mc6845_address] )
			{
				m_mc6845_locked_register[m_mc6845_address] = 1;
			}
		}
		break;

	case 0x8:
		/* Check if we're changing to graphics mode 2 */
		if ( ( m_mode_control & 0x12 ) != 0x12 && ( data & 0x12 ) == 0x12 )
		{
			m_write = 0x0F;
		}
		else
		{
			membank("bank1")->set_base(m_vram + isa8_cga_pc1512_device::vram_offset[0]);
		}
		m_mode_control = data;
		switch( m_mode_control & 0x3F )
		{
		case 0x08: case 0x09: case 0x0C: case 0x0D:
			mc6845->set_hpixels_per_column( 8 );
			m_update_row = cga_text_inten_update_row;
			break;
		case 0x0A: case 0x0B: case 0x2A: case 0x2B:
			mc6845->set_hpixels_per_column( 8 );
			if ( ( CGA_MONITOR ) == CGA_MONITOR_COMPOSITE )
			{
				m_update_row = cga_gfx_4bppl_update_row;
			}
			else
			{
				m_update_row = cga_gfx_2bpp_update_row;
			}
			break;
		case 0x0E: case 0x0F: case 0x2E: case 0x2F:
			mc6845->set_hpixels_per_column( 8 );
			m_update_row = cga_gfx_2bpp_update_row;
			break;
		case 0x18: case 0x19: case 0x1C: case 0x1D:
			mc6845->set_hpixels_per_column( 8 );
			m_update_row = cga_text_inten_alt_update_row;
			break;
		case 0x1A: case 0x1B: case 0x3A: case 0x3B:
			mc6845->set_hpixels_per_column( 8 );
			m_update_row = pc1512_gfx_4bpp_update_row;
			break;
		case 0x1E: case 0x1F: case 0x3E: case 0x3F:
			mc6845->set_hpixels_per_column( 16 );
			m_update_row = cga_gfx_1bpp_update_row;
			break;
		case 0x28: case 0x29: case 0x2C: case 0x2D:
			mc6845->set_hpixels_per_column( 8 );
			m_update_row = cga_text_blink_update_row;
			break;
		case 0x38: case 0x39: case 0x3C: case 0x3D:
			mc6845->set_hpixels_per_column( 8 );
			m_update_row = cga_text_blink_alt_update_row;
			break;
		default:
			m_update_row = NULL;
			break;
		}
		break;

	case 0xd:
		m_write = data;
		break;

	case 0xe:
		m_read = data;
		if ( ( m_mode_control & 0x12 ) == 0x12 )
		{
			membank("bank1")->set_base(m_vram + isa8_cga_pc1512_device::vram_offset[data & 3]);
		}
		break;

	default:
		isa8_cga_device::io_write(space, offset,data);
		break;
	}
}


READ8_MEMBER( isa8_cga_pc1512_device::io_read )
{
	UINT8 data;

	switch (offset)
	{
	case 0xd:
		data = m_write;
		break;

	case 0xe:
		data = m_read;
		break;

	default:
		data = isa8_cga_device::io_read(space, offset);
		break;
	}
	return data;
}


WRITE8_MEMBER( isa8_cga_pc1512_device::vram_w )
{
	if ( ( m_mode_control & 0x12 ) == 0x12 )
	{
		if (m_write & 1)
			m_vram[offset+isa8_cga_pc1512_device::vram_offset[0]] = data; /* blue plane */
		if (m_write & 2)
			m_vram[offset+isa8_cga_pc1512_device::vram_offset[1]] = data; /* green */
		if (m_write & 4)
			m_vram[offset+isa8_cga_pc1512_device::vram_offset[2]] = data; /* red */
		if (m_write & 8)
			m_vram[offset+isa8_cga_pc1512_device::vram_offset[3]] = data; /* intensity (text, 4color) */
	}
	else
	{
		m_vram[offset + isa8_cga_pc1512_device::vram_offset[0]] = data;
	}
}


const device_type ISA8_CGA_PC1512 = &device_creator<isa8_cga_pc1512_device>;

const offs_t isa8_cga_pc1512_device::vram_offset[4]= { 0x0000, 0x4000, 0x8000, 0xC000 };
const UINT8 isa8_cga_pc1512_device::mc6845_writeonce_register[31] =
{
	1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

//-------------------------------------------------
//  isa8_cga_pc1512_device - constructor
//-------------------------------------------------

isa8_cga_pc1512_device::isa8_cga_pc1512_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		isa8_cga_device( mconfig, ISA8_CGA_PC1512, "ISA8_CGA_PC1512", tag, owner, clock, "cga_pc1512", __FILE__)
{
	m_vram_size = 0x10000;
	m_chr_gen_offset[0] = 0x0000;
	m_chr_gen_offset[1] = 0x0800;
	m_chr_gen_offset[2] = 0x1000;
	m_chr_gen_offset[3] = 0x1800;
}


const rom_entry *isa8_cga_pc1512_device::device_rom_region() const
{
	return NULL;
}


ioport_constructor isa8_cga_pc1512_device::device_input_ports() const
{
	return INPUT_PORTS_NAME( pc1512 );
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void isa8_cga_pc1512_device::device_start()
{
	isa8_cga_device::device_start();

	m_isa->install_device(0x3d0, 0x3df, 0, 0, read8_delegate( FUNC(isa8_cga_pc1512_device::io_read), this ), write8_delegate( FUNC(isa8_cga_pc1512_device::io_write), this ) );
	m_isa->install_bank(0xb8000, 0xbbfff, 0, 0, "bank1", m_vram);

	address_space &space = machine().firstcpu->space( AS_PROGRAM );

	space.install_write_handler( 0xb8000, 0xbbfff, 0, 0x0C000, write8_delegate( FUNC(isa8_cga_pc1512_device::vram_w), this ) );
}

void isa8_cga_pc1512_device::device_reset()
{
	isa8_cga_device::device_reset();

	m_write = 0x0f;
	m_read = 0;
	m_mc6845_address = 0;
	for ( int i = 0; i < 31; i++ )
	{
		m_mc6845_locked_register[i] = 0;
	}

	membank("bank1")->set_base(m_vram + isa8_cga_pc1512_device::vram_offset[0]);
}

void isa8_wyse700_device::change_resolution(UINT8 mode)
{
	int width = 0, height = 0;
	if (mode & 2) {
		machine().root_device().membank("bank_wy1")->set_base(m_vram + 0x10000);
	} else {
		machine().root_device().membank("bank_wy1")->set_base(m_vram);
	}
	if ((m_control & 0xf0) == (mode & 0xf0)) return;

	switch(mode & 0xf0) {
		case 0xc0: width = 1280; height = 800; break;
		case 0xa0: width = 1280; height = 400; break;
		case 0x80: width = 640; height = 400; break;
		case 0x00: width = 640; height = 400; break; // unhandled
	}
	rectangle visarea(0, width-1, 0, height-1);
	subdevice<screen_device>(CGA_SCREEN_NAME)->configure(width, height, visarea, HZ_TO_ATTOSECONDS(60));

}

WRITE8_MEMBER( isa8_wyse700_device::io_write )
{
	switch (offset)
	{
	case 0xd:
		m_bank_offset = data;
		break;

	case 0xe:
		m_bank_base = data;
		break;

	case 0xf:
		change_resolution(data);
		m_control = data;
		break;
	default:
		isa8_cga_device::io_write(space, offset,data);
		break;
	}
}


READ8_MEMBER( isa8_wyse700_device::io_read )
{
	UINT8 data;

	switch (offset)
	{
	case 0xd:
		data = m_bank_offset;
		break;

	case 0xe:
		data = m_bank_base;
		break;

	case 0xf:
		data = m_control;
		break;
	default:
		data = isa8_cga_device::io_read(space, offset);
		break;
	}
	return data;
}


const device_type ISA8_WYSE700 = &device_creator<isa8_wyse700_device>;


//-------------------------------------------------
//  isa8_wyse700_device - constructor
//-------------------------------------------------

isa8_wyse700_device::isa8_wyse700_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
		isa8_cga_device( mconfig, ISA8_WYSE700, "Wyse 700", tag, owner, clock, "wyse700", __FILE__)
{
	m_vram_size = 0x20000;
	m_start_offset = 0x18000;
}

/*
Character ROMs:

250211-03.e5: Character ROM  Label: "(C) WYSE TECH / REV.A / 250211-03"
250212-03.f5: Character ROM  Label: "(C) WYSE TECH / REV.A / 250212-03"

Not dumped:

250026-03.2d: MC68705 MCU  Label: "(C) WYSE TECH / REV.1 / 250026-03"
250270-01.8b: PAL?         Label: "250270-01"
250024-01.8g: PAL?         Label: "250024-01"
250210-01.c2: PAL?         Label: "250210-01"
*/
ROM_START( wyse700 )
	ROM_REGION(0x4000,"gfx1", 0)
	ROM_LOAD( "250211-03.e5", 0x0000, 0x2000, CRC(58b61f63) SHA1(29ecb7cf7d07d692f0fc54e2dea8389f17a65f1a))
	ROM_LOAD( "250212-03.f5", 0x2000, 0x2000, CRC(6930d741) SHA1(1beeb133c5e39eee9914bdc5924039d70b5edcad))
ROM_END

const rom_entry *isa8_wyse700_device::device_rom_region() const
{
	return ROM_NAME( wyse700 );
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void isa8_wyse700_device::device_start()
{
	isa8_cga_device::device_start();

	m_isa->install_device(0x3d0, 0x3df, 0, 0, read8_delegate( FUNC(isa8_wyse700_device::io_read), this ), write8_delegate( FUNC(isa8_wyse700_device::io_write), this ) );
	m_isa->install_bank(0xa0000, 0xaffff, 0, 0, "bank_wy1", m_vram);
	m_isa->install_bank(0xb0000, 0xbffff, 0, 0, "bank_cga", m_vram + 0x10000);
}

void isa8_wyse700_device::device_reset()
{
	isa8_cga_device::device_reset();
	m_control = 0;
	m_bank_offset = 0;
	m_bank_base = 0;
}

UINT32 isa8_wyse700_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	if (m_control & 0x08) {
		const rgb_t *palette = palette_entry_list_raw(bitmap.palette());
		UINT8 fg = m_color_select & 0x0F;
		UINT32 addr = 0;
		for (int y = 0; y < 800; y++) {
			UINT8 *src = m_vram + addr;

			if (y & 1) {
				src += 0x10000;
				addr += 160;
			}

			for (int x = 0; x < (1280 / 8); x++) {
				UINT8 val = src[x];

				for (int i = 0; i < 8; i++) {
					bitmap.pix32(y,x*8+i) = (val & 0x80) ? palette[fg] : palette[0x00];
					val <<= 1;
				}
			}
		}
	} else {
		return isa8_cga_device::screen_update(screen, bitmap, cliprect);
	}
	return 0;
}
