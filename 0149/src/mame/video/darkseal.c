/***************************************************************************

   Dark Seal Video emulation - Bryan McPhail, mish@tendril.co.uk

****************************************************************************

 uses 2x DECO55 tilemaps

**************************************************************************

 Sprite/Tilemap Priority Note (is this implemented?)

    Word 4:
        Mask 0x8000 - ?
        Mask 0x4000 - Sprite is drawn beneath top 8 pens of playfield 4
        Mask 0x3e00 - Colour (32 palettes, most games only use 16)
        Mask 0x01ff - X coordinate

***************************************************************************/

#include "emu.h"
#include "includes/darkseal.h"
#include "video/deco16ic.h"

/***************************************************************************/

/******************************************************************************/

void darkseal_state::update_24bitcol(int offset)
{
	int r,g,b;

	r = (m_generic_paletteram_16[offset] >> 0) & 0xff;
	g = (m_generic_paletteram_16[offset] >> 8) & 0xff;
	b = (m_generic_paletteram2_16[offset] >> 0) & 0xff;

	palette_set_color(machine(),offset,MAKE_RGB(r,g,b));
}

WRITE16_MEMBER(darkseal_state::darkseal_palette_24bit_rg_w)
{
	COMBINE_DATA(&m_generic_paletteram_16[offset]);
	update_24bitcol(offset);
}

WRITE16_MEMBER(darkseal_state::darkseal_palette_24bit_b_w)
{
	COMBINE_DATA(&m_generic_paletteram2_16[offset]);
	update_24bitcol(offset);
}

/******************************************************************************/

void darkseal_state::video_start()
{
}

/******************************************************************************/

UINT32 darkseal_state::screen_update_darkseal(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	machine().tilemap().set_flip_all(m_flipscreen ? (TILEMAP_FLIPY | TILEMAP_FLIPX) : 0);

	bitmap.fill(get_black_pen(machine()), cliprect);

	deco16ic_pf_update(m_deco_tilegen1, m_pf1_rowscroll, m_pf1_rowscroll);
	deco16ic_pf_update(m_deco_tilegen2, m_pf3_rowscroll, m_pf3_rowscroll);

	deco16ic_tilemap_1_draw(m_deco_tilegen2, bitmap, cliprect, 0, 0);
	deco16ic_tilemap_2_draw(m_deco_tilegen2, bitmap, cliprect, 0, 0);

	deco16ic_tilemap_1_draw(m_deco_tilegen1, bitmap, cliprect, 0, 0);
	m_sprgen->draw_sprites(bitmap, cliprect, m_spriteram->buffer(), 0x400);
	deco16ic_tilemap_2_draw(m_deco_tilegen1, bitmap, cliprect, 0, 0);

	return 0;
}

/******************************************************************************/
