/*********************************************************************

    68561mpcc.h

    Rockwell 68561 MPCC (Multi Protocol Communications Controller)

    skeleton driver

*********************************************************************/

#ifndef __68561MPCC_H__
#define __68561MPCC_H__

#define MCFG_MPCC68561_ADD(_tag, _clock, _intrq_cb) \
	MCFG_DEVICE_ADD(_tag, MPCC68561, _clock)    \
	downcast<mpcc68561_t *>(device)->set_intrq_cb(_intrq_cb);

class mpcc68561_t : public device_t
{
public:
	enum IRQType_t {
		IRQ_NONE,
		IRQ_A_RX,
		IRQ_A_RX_SPECIAL,
		IRQ_B_RX,
		IRQ_B_RX_SPECIAL,
		IRQ_A_TX,
		IRQ_B_TX,
		IRQ_A_EXT,
		IRQ_B_EXT,
	};

	mpcc68561_t(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);
	void set_intrq_cb(line_cb_t cb);

	UINT8 get_reg_a(int reg);
	void set_reg_a(int reg, UINT8 data);

	void set_status(int status);

	DECLARE_READ8_MEMBER(reg_r);
	DECLARE_WRITE8_MEMBER(reg_w);

protected:
	virtual void device_start();
	virtual void device_reset();
	virtual void device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr);

private:
	struct Chan {
		bool txIRQEnable;
		bool rxIRQEnable;
		bool extIRQEnable;
		bool baudIRQEnable;
		bool txIRQPending;
		bool rxIRQPending;
		bool extIRQPending;
		bool baudIRQPending;
		bool txEnable;
		bool rxEnable;
		bool txUnderrun;
		bool txUnderrunEnable;
		bool syncHunt;
		bool DCDEnable;
		bool CTSEnable;
		UINT8 rxData;
		UINT8 txData;

		emu_timer *baudtimer;

		UINT8 reg_val[22];
	};

	int mode;
	int reg;
	int status;
	int IRQV;
	int MasterIRQEnable;
	int lastIRQStat;
	IRQType_t IRQType;

	Chan channel[1];

	line_cb_t intrq_cb;

	void updateirqs();
	void initchannel(int ch);
	void resetchannel(int ch);
	void acknowledge();
	UINT8 getreg();
	void putreg(int ch, UINT8 data);
};

/***************************************************************************
    MACROS
***************************************************************************/

extern const device_type MPCC68561;

#endif /* __68561MPCC_H__ */
