/***************************************************************************

    Venture Line Super Rider driver

***************************************************************************/

#include "emu.h"
#include "includes/suprridr.h"


/*************************************
 *
 *  Tilemap callbacks
 *
 *************************************/

TILE_GET_INFO_MEMBER(suprridr_state::get_tile_info)
{
	UINT8 code = m_bgram[tile_index];
	SET_TILE_INFO_MEMBER(0, code, 0, 0);
}


TILE_GET_INFO_MEMBER(suprridr_state::get_tile_info2)
{
	UINT8 code = m_fgram[tile_index];
	SET_TILE_INFO_MEMBER(1, code, 0, 0);
}



/*************************************
 *
 *  Video startup
 *
 *************************************/

void suprridr_state::video_start()
{
	m_fg_tilemap          = &machine().tilemap().create(tilemap_get_info_delegate(FUNC(suprridr_state::get_tile_info2),this), TILEMAP_SCAN_ROWS,  8,8, 32,32);
	m_bg_tilemap          = &machine().tilemap().create(tilemap_get_info_delegate(FUNC(suprridr_state::get_tile_info),this),  TILEMAP_SCAN_ROWS,       8,8, 32,32);
	m_bg_tilemap_noscroll = &machine().tilemap().create(tilemap_get_info_delegate(FUNC(suprridr_state::get_tile_info),this),  TILEMAP_SCAN_ROWS,       8,8, 32,32);

	m_fg_tilemap->set_transparent_pen(0);
}



/*************************************
 *
 *  Color PROM decoding
 *
 *************************************/

void suprridr_state::palette_init()
{
	const UINT8 *color_prom = memregion("proms")->base();
	int i;

	for (i = 0; i < 96; i++)
	{
		int bit0,bit1,bit2,r,g,b;

		/* red component */
		bit0 = (*color_prom >> 0) & 0x01;
		bit1 = (*color_prom >> 1) & 0x01;
		bit2 = (*color_prom >> 2) & 0x01;
		r = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		/* green component */
		bit0 = (*color_prom >> 3) & 0x01;
		bit1 = (*color_prom >> 4) & 0x01;
		bit2 = (*color_prom >> 5) & 0x01;
		g = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
		/* blue component */
		bit0 = (*color_prom >> 6) & 0x01;
		bit1 = (*color_prom >> 7) & 0x01;
		b = 0x4f * bit0 + 0xa8 * bit1;

		palette_set_color(machine(),i,MAKE_RGB(r,g,b));
		color_prom++;
	}
}



/*************************************
 *
 *  Screen flip/scroll registers
 *
 *************************************/

WRITE8_MEMBER(suprridr_state::suprridr_flipx_w)
{
	m_flipx = data & 1;
	machine().tilemap().set_flip_all((m_flipx ? TILEMAP_FLIPX : 0) | (m_flipy ? TILEMAP_FLIPY : 0));
}


WRITE8_MEMBER(suprridr_state::suprridr_flipy_w)
{
	m_flipy = data & 1;
	machine().tilemap().set_flip_all((m_flipx ? TILEMAP_FLIPX : 0) | (m_flipy ? TILEMAP_FLIPY : 0));
}


WRITE8_MEMBER(suprridr_state::suprridr_fgdisable_w)
{
	m_fg_tilemap->enable(~data & 1);
}


WRITE8_MEMBER(suprridr_state::suprridr_fgscrolly_w)
{
	m_fg_tilemap->set_scrolly(0, data);
}


WRITE8_MEMBER(suprridr_state::suprridr_bgscrolly_w)
{
	m_bg_tilemap->set_scrolly(0, data);
}


int suprridr_state::suprridr_is_screen_flipped()
{
	return m_flipx;  /* or is it flipy? */
}



/*************************************
 *
 *  Video RAM writes
 *
 *************************************/

WRITE8_MEMBER(suprridr_state::suprridr_bgram_w)
{
	m_bgram[offset] = data;
	m_bg_tilemap->mark_tile_dirty(offset);
	m_bg_tilemap_noscroll->mark_tile_dirty(offset);
}


WRITE8_MEMBER(suprridr_state::suprridr_fgram_w)
{
	m_fgram[offset] = data;
	m_fg_tilemap->mark_tile_dirty(offset);
}



/*************************************
 *
 *  Video refresh
 *
 *************************************/

UINT32 suprridr_state::screen_update_suprridr(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	UINT8 *spriteram = m_spriteram;
	rectangle subclip;
	int i;
	const rectangle &visarea = screen.visible_area();

	/* render left 4 columns with no scroll */
	subclip = visarea;;
	subclip.max_x = subclip.min_x + (m_flipx ? 1*8 : 4*8) - 1;
	subclip &= cliprect;
	m_bg_tilemap_noscroll->draw(bitmap, subclip, 0, 0);

	/* render right 1 column with no scroll */
	subclip = visarea;;
	subclip.min_x = subclip.max_x - (m_flipx ? 4*8 : 1*8) + 1;
	subclip &= cliprect;
	m_bg_tilemap_noscroll->draw(bitmap, subclip, 0, 0);

	/* render the middle columns normally */
	subclip = visarea;;
	subclip.min_x += m_flipx ? 1*8 : 4*8;
	subclip.max_x -= m_flipx ? 4*8 : 1*8;
	subclip &= cliprect;
	m_bg_tilemap->draw(bitmap, subclip, 0, 0);

	/* render the top layer */
	m_fg_tilemap->draw(bitmap, cliprect, 0, 0);

	/* draw the sprites */
	for (i = 0; i < 48; i++)
	{
		int code = (spriteram[i*4+1] & 0x3f) | ((spriteram[i*4+2] >> 1) & 0x40);
		int color = spriteram[i*4+2] & 0x7f;
		int fx = spriteram[i*4+1] & 0x40;
		int fy = spriteram[i*4+1] & 0x80;
		int x = spriteram[i*4+3];
		int y = 240 - spriteram[i*4+0];

		if (m_flipx)
		{
			fx = !fx;
			x = 240 - x;
		}
		if (m_flipy)
		{
			fy = !fy;
			y = 240 - y;
		}
		drawgfx_transpen(bitmap, cliprect, machine().gfx[2], code, color, fx, fy, x, y, 0);
	}
	return 0;
}
