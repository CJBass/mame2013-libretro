class tp84_state : public driver_device
{
public:
	tp84_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_audiocpu(*this, "audiocpu"),
		m_palette_bank(*this, "palette_bank"),
		m_flipscreen_x(*this, "flipscreen_x"),
		m_flipscreen_y(*this, "flipscreen_y"),
		m_scroll_x(*this, "scroll_x"),
		m_scroll_y(*this, "scroll_y"),
		m_bg_videoram(*this, "bg_videoram"),
		m_fg_videoram(*this, "fg_videoram"),
		m_bg_colorram(*this, "bg_colorram"),
		m_fg_colorram(*this, "fg_colorram"),
		m_spriteram(*this, "spriteram"){ }

	required_device<cpu_device> m_audiocpu;
	required_shared_ptr<UINT8> m_palette_bank;
	required_shared_ptr<UINT8> m_flipscreen_x;
	required_shared_ptr<UINT8> m_flipscreen_y;
	required_shared_ptr<UINT8> m_scroll_x;
	required_shared_ptr<UINT8> m_scroll_y;
	required_shared_ptr<UINT8> m_bg_videoram;
	required_shared_ptr<UINT8> m_fg_videoram;
	required_shared_ptr<UINT8> m_bg_colorram;
	required_shared_ptr<UINT8> m_fg_colorram;
	required_shared_ptr<UINT8> m_spriteram;
	tilemap_t *m_bg_tilemap;
	tilemap_t *m_fg_tilemap;

	UINT8 m_sub_irq_mask;
	DECLARE_READ8_MEMBER(tp84_sh_timer_r);
	DECLARE_WRITE8_MEMBER(tp84_filter_w);
	DECLARE_WRITE8_MEMBER(tp84_sh_irqtrigger_w);
	DECLARE_WRITE8_MEMBER(sub_irq_mask_w);
	DECLARE_WRITE8_MEMBER(tp84_spriteram_w);
	DECLARE_READ8_MEMBER(tp84_scanline_r);
	TILE_GET_INFO_MEMBER(get_bg_tile_info);
	TILE_GET_INFO_MEMBER(get_fg_tile_info);
	virtual void machine_start();
	virtual void video_start();
	virtual void palette_init();
	UINT32 screen_update_tp84(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
	INTERRUPT_GEN_MEMBER(sub_vblank_irq);
	void draw_sprites(bitmap_ind16 &bitmap, const rectangle &cliprect);
};
