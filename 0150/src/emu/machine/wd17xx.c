/***************************************************************************

    wd17xx.c

    Implementations of the Western Digital 17xx and 27xx families of
    floppy disk controllers


    Models:

              DAL   DD   Side   Clock       Remark
      ---------------------------------------------------------
      FD1771                    1 or 2 MHz  First model
      FD1781         x          1 or 2 MHz
      FD1791         x          1 or 2 MHz
      FD1792                    1 or 2 MHz
      FD1793   x     x          1 or 2 MHz
      FD1794   x                1 or 2 MHz
      FD1795         x     x    1 or 2 MHz
      FD1797   x     x     x    1 or 2 MHz
      FD1761         x          1 MHz
      FD1762                    1 MHz       ?
      FD1763   x     x          1 MHz
      FD1764   x                1 MHz       ?
      FD1765         x     x    1 MHz
      FD1767   x     x     x    1 MHz
      WD2791         x          1 or 2 MHz  Internal data separator
      WD2793   x     x          1 or 2 MHz  Internal data separator
      WD2795         x     x    1 or 2 MHz  Internal data separator
      WD2797   x     x     x    1 or 2 MHz  Internal data separator
      WD1770   x     x          8 MHz       Motor On signal
      WD1772   x     x          8 MHz       Motor On signal, Faster stepping rates
      WD1773   x     x          8 MHz       Enable precomp line

      Notes: - In general, later models include all features of earlier models
             - DAL: Data access lines, x = TRUE; otherwise inverted
             - DD: Double density support
             - Side: Has side select support
             - ?: Unknown if it really exists

    Clones:

      - SMC FD179x
      - Fujitsu MB8876 -> FD1791, MB8877 -> FD1793
      - VLSI VL177x


    Changelog:

    Kevin Thacker
        - Removed disk image code and replaced it with floppy drive functions.
          Any disc image is now useable with this code.
        - Fixed write protect

    2005-Apr-16 P.Harvey-Smith:
        - Increased the delay in wd17xx_timed_read_sector_request and
          wd17xx_timed_write_sector_request, to 40us to more closely match
          what happens on the real hardware, this has fixed the NitrOS9 boot
          problems.

    2007-Nov-01 Wilbert Pol:
        Needed these changes to get the MB8877 for Osborne-1 to work:
        - Added support for multiple record read
        - Changed the wd17xx_read_id to not return after DATADONEDELAY, but
          the host should read the id data through the data register. This
          was accomplished by making this change in the wd17xx_read_id
          function:
            -               wd17xx_complete_command(device, DELAY_DATADONE);
            +               wd17xx_set_data_request();

    2009-May-10 Robbbert:
        Further change to get the Kaypro II to work
        - When wd17xx_read_id has generated the 6 data bytes, it should make
          an IRQ and turn off the busy status. The timing for Osborne is
          critical, it must be between 300 and 700 usec, I've settled on 400.
          The Kaypro doesn't care timewise, but the busy flag must turn off
          sometime or it hangs.
            -       w->status |= STA_2_BUSY;
            +       wd17xx_set_busy(device, attotime::from_usec(400));

    2009-June-4 Roberto Lavarone:
        - Initial support for wd1771
        - Added simulation of head loaded feedback from drive
        - Bugfix: busy flag was cleared too early

    2009-June-21 Robbbert:
    - The Bugfix above, while valid, caused the Osborne1 to fail. This
      is because the delay must not exceed 14usec (found by extensive testing).
    - The minimum delay is 1usec, need by z80netf while formatting a disk.
    - http://www.bannister.org/forums/ubbthreads.php?ubb=showflat&Number=50889#Post50889
      explains the problems, testing done, and the test procedure for the z80netf.
    - Thus, the delay is set to 10usec, and all the disks I have (not many)
      seem to work.
    - Note to anyone who wants to change something: Make sure that the
      Osborne1 boots up! It is extremely sensitive to timing!
    - For testing only: The osborne1 rom can be patched to make it much
      more stable, by changing the byte at 0x0da7 from 0x28 to 0x18.

    2009-June-25 Robbbert:
    - Turns out kayproii not working, 10usec is too short.. but 11usec is ok.
      Setting it to 12usec.
      Really, this whole thing needs a complete rewrite.

    2009-July-08 Roberto Lavarone:
    - Fixed a bug in head load flag handling: einstein and samcoupe now working again

    2009-September-30 Robbbert:
    - Modified what status flags are returned after a Forced Interrupt,
      to allow Microbee to boot CP/M.

    2010-Jan-31 Phill Harvey-Smith
    - The above bugfixes for the Kaypro/Osborne1 have borked the booting on the Dragon
      Alpha. The Alpha it seems needs a delay of ay least 17us or the NMI generated by
      INTRQ happens too early and doen't break out of the read/write bytes loops.

      I have made the delay settable by calling wd17xx_set_complete_command_delay, and
      let it default to 12us, as required above, so that the Dragon Alpha works again.
      This hopefully should not break the other machines.

      This should probably be considdered a minor hack but it does seem to work !

    2010-02-04 Phill Harvey-Smith
    - Added multiple sector write as the RM Nimbus needs it.

    2010-March-22 Curt Coder:
    - Implemented immediate and index pulse interrupts.

    2010-Dec-31 Phill Harvey-Smith
    - Copied multi-sector write code from r7263, for some reason this had been
      silently removed, but is required for the rmnimbus driver.

    2011-Mar-08 Phill Harvey-Smith
    - Triggering intrq now clears the DRQ bit in the status as well as the busy bit.
      Execution of the READ_DAM command now correctly sets w->command.

    2011-Apr-01 Curt Coder
    - Set complete command delay to 16 usec (DD) / 32 usec (SD) and removed
      the external delay setting hack.

    2011-Jun-24 Curt Coder
    - Added device types for all known variants, and enforced inverted DAL lines.

    2011-Sep-18 Curt Coder
    - Connected Side Select Output for variants that support it.

    TODO:
        - What happens if a track is read that doesn't have any id's on it?
         (e.g. unformatted disc)
        - Rewrite into a C++ device

***************************************************************************/


#include "emu.h"
#include "formats/imageutl.h"
#include "machine/wd17xx.h"
#include "imagedev/flopdrv.h"
#include "devlegcy.h"

/***************************************************************************
    CONSTANTS
***************************************************************************/

#define VERBOSE         0   /* General logging */
#define VERBOSE_DATA    0   /* Logging of each byte during read and write */

#define DELAY_ERROR     3
#define DELAY_NOTREADY  1
#define DELAY_DATADONE  3

#define TYPE_I          1
#define TYPE_II         2
#define TYPE_III        3
#define TYPE_IV         4

#define FDC_STEP_RATE   0x03    /* Type I additional flags */
#define FDC_STEP_VERIFY 0x04    /* verify track number */
#define FDC_STEP_HDLOAD 0x08    /* load head */
#define FDC_STEP_UPDATE 0x10    /* update track register */

#define FDC_RESTORE     0x00    /* Type I commands */
#define FDC_SEEK        0x10
#define FDC_STEP        0x20
#define FDC_STEP_IN     0x40
#define FDC_STEP_OUT    0x60

#define FDC_MASK_TYPE_I         (FDC_STEP_HDLOAD|FDC_STEP_VERIFY|FDC_STEP_RATE)

/* Type I commands status */
#define STA_1_BUSY      0x01    /* controller is busy */
#define STA_1_IPL       0x02    /* index pulse */
#define STA_1_TRACK0    0x04    /* track 0 detected */
#define STA_1_CRC_ERR   0x08    /* CRC error */
#define STA_1_SEEK_ERR  0x10    /* seek error */
#define STA_1_HD_LOADED 0x20    /* head loaded */
#define STA_1_WRITE_PRO 0x40    /* floppy is write protected */
#define STA_1_NOT_READY 0x80    /* drive not ready */
#define STA_1_MOTOR_ON  0x80    /* status of the Motor On output (WD1770 and WD1772 only) */

/* Type II and III additional flags */
#define FDC_DELETED_AM  0x01    /* read/write deleted address mark */
#define FDC_SIDE_CMP_T  0x02    /* side compare track data */
#define FDC_15MS_DELAY  0x04    /* delay 15ms before command */
#define FDC_SIDE_CMP_S  0x08    /* side compare sector data */
#define FDC_MULTI_REC   0x10    /* only for type II commands */

/* Type II commands */
#define FDC_READ_SEC    0x80    /* read sector */
#define FDC_WRITE_SEC   0xA0    /* write sector */

#define FDC_MASK_TYPE_II        (FDC_MULTI_REC|FDC_SIDE_CMP_S|FDC_15MS_DELAY|FDC_SIDE_CMP_T|FDC_DELETED_AM)

/* Type II commands status */
#define STA_2_BUSY      0x01
#define STA_2_DRQ       0x02
#define STA_2_LOST_DAT  0x04
#define STA_2_CRC_ERR   0x08
#define STA_2_REC_N_FND 0x10
#define STA_2_REC_TYPE  0x20
#define STA_2_WRITE_PRO 0x40
#define STA_2_NOT_READY 0x80

#define FDC_MASK_TYPE_III       (FDC_SIDE_CMP_S|FDC_15MS_DELAY|FDC_SIDE_CMP_T|FDC_DELETED_AM)

/* Type III commands */
#define FDC_READ_DAM    0xc0    /* read data address mark */
#define FDC_READ_TRK    0xe0    /* read track */
#define FDC_WRITE_TRK   0xf0    /* write track (format) */

/* Type IV additional flags */
#define FDC_IM0         0x01    /* interrupt mode 0 */
#define FDC_IM1         0x02    /* interrupt mode 1 */
#define FDC_IM2         0x04    /* interrupt mode 2 */
#define FDC_IM3         0x08    /* interrupt mode 3 */

#define FDC_MASK_TYPE_IV        (FDC_IM3|FDC_IM2|FDC_IM1|FDC_IM0)

/* Type IV commands */
#define FDC_FORCE_INT   0xd0    /* force interrupt */

/* structure describing a double density track */
#define TRKSIZE_DD      6144
#if 0
static const UINT8 track_DD[][2] = {
	{16, 0x4e},     /* 16 * 4E (track lead in)               */
	{ 8, 0x00},     /*  8 * 00 (pre DAM)                     */
	{ 3, 0xf5},     /*  3 * F5 (clear CRC)                   */

	{ 1, 0xfe},     /* *** sector *** FE (DAM)               */
	{ 1, 0x80},     /*  4 bytes track,head,sector,seclen     */
	{ 1, 0xf7},     /*  1 * F7 (CRC)                         */
	{22, 0x4e},     /* 22 * 4E (sector lead in)              */
	{12, 0x00},     /* 12 * 00 (pre AM)                      */
	{ 3, 0xf5},     /*  3 * F5 (clear CRC)                   */
	{ 1, 0xfb},     /*  1 * FB (AM)                          */
	{ 1, 0x81},     /*  x bytes sector data                  */
	{ 1, 0xf7},     /*  1 * F7 (CRC)                         */
	{16, 0x4e},     /* 16 * 4E (sector lead out)             */
	{ 8, 0x00},     /*  8 * 00 (post sector)                 */
	{ 0, 0x00},     /* end of data                           */
};
#endif

/* structure describing a single density track */
#define TRKSIZE_SD      3172
#if 0
static const UINT8 track_SD[][2] = {
	{16, 0xff},     /* 16 * FF (track lead in)               */
	{ 8, 0x00},     /*  8 * 00 (pre DAM)                     */
	{ 1, 0xfc},     /*  1 * FC (clear CRC)                   */

	{11, 0xff},     /* *** sector *** 11 * FF                */
	{ 6, 0x00},     /*  6 * 00 (pre DAM)                     */
	{ 1, 0xfe},     /*  1 * FE (DAM)                         */
	{ 1, 0x80},     /*  4 bytes track,head,sector,seclen     */
	{ 1, 0xf7},     /*  1 * F7 (CRC)                         */
	{10, 0xff},     /* 10 * FF (sector lead in)              */
	{ 4, 0x00},     /*  4 * 00 (pre AM)                      */
	{ 1, 0xfb},     /*  1 * FB (AM)                          */
	{ 1, 0x81},     /*  x bytes sector data                  */
	{ 1, 0xf7},     /*  1 * F7 (CRC)                         */
	{ 0, 0x00},     /* end of data                           */
};
#endif


/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

struct wd1770_state
{
	/* callbacks */
	devcb_resolved_read_line in_dden_func;
	devcb_resolved_write_line out_intrq_func;
	devcb_resolved_write_line out_drq_func;

	/* input lines */
	int mr;            /* master reset */
	int rdy;           /* ready, enable precomp */
	int tr00;          /* track 00 */
	int idx;           /* index */
	int wprt;          /* write protect */
	int dden;          /* double density */

	/* output lines */
	int mo;            /* motor on */
	int dirc;          /* direction */
	int drq;           /* data request */
	int intrq;         /* interrupt request */

	/* register */
	UINT8 data_shift;
	UINT8 data;
	UINT8 track;
	UINT8 sector;
	UINT8 command;
	UINT8 status;
	UINT8 interrupt;

	int stepping_rate[4];  /* track stepping rate in ms */

		unsigned short  crc;    /* Holds the current CRC value for write_track CRC calculation */
		int     crc_active; /* Flag indicating that CRC calculation in write_track is active. */

	UINT8   track_reg;              /* value of track register */
	UINT8   command_type;           /* last command type */
	UINT8   head;                   /* current head # */

	UINT8   read_cmd;               /* last read command issued */
	UINT8   write_cmd;              /* last write command issued */
	INT8    direction;              /* last step direction */
	UINT8   last_command_data;      /* last command data */

	UINT8   status_drq;             /* status register data request bit */
	UINT8   busy_count;             /* how long to keep busy bit set */

	UINT8   buffer[6144];           /* I/O buffer (holds up to a whole track) */
	UINT32  data_offset;            /* offset into I/O buffer */
	INT32   data_count;             /* transfer count from/into I/O buffer */

	UINT8   *fmt_sector_data[256];  /* pointer to data after formatting a track */

	UINT8   dam_list[256][4];       /* list of data address marks while formatting */
	int     dam_data[256];          /* offset to data inside buffer while formatting */
	int     dam_cnt;                /* valid number of entries in the dam_list */
	UINT16  sector_length;          /* sector length (byte) */

	UINT8   ddam;                   /* ddam of sector found - used when reading */
	UINT8   sector_data_id;
	int     data_direction;

	int     hld_count;              /* head loaded counter */

	/* timers to delay execution/completion of commands */
	emu_timer *timer_cmd, *timer_data, *timer_rs, *timer_ws;

	/* this is the drive currently selected */
	device_t *drive;

	/* this is the head currently selected */
	UINT8 hd;

	/* pause time when writing/reading sector */
	int pause_time;

	/* Were we busy when we received a FORCE_INT command */
	UINT8   was_busy;

	/* Pointer to interface */
	const wd17xx_interface *intf;
};


/***************************************************************************
    DEFAULT INTERFACES
***************************************************************************/

const wd17xx_interface default_wd17xx_interface =
{
	DEVCB_NULL, DEVCB_NULL, DEVCB_NULL, { FLOPPY_0, FLOPPY_1, FLOPPY_2, FLOPPY_3}
};

const wd17xx_interface default_wd17xx_interface_2_drives =
{
	DEVCB_NULL, DEVCB_NULL, DEVCB_NULL, { FLOPPY_0, FLOPPY_1, NULL, NULL}
};


/***************************************************************************
    PROTOTYPES
***************************************************************************/

static void wd17xx_complete_command(device_t *device, int delay);
static void wd17xx_timed_data_request(device_t *device);
static int wd17xx_locate_sector(device_t *device);
static void wd17xx_timed_read_sector_request(device_t *device);


/*****************************************************************************
    INLINE FUNCTIONS
*****************************************************************************/

INLINE wd1770_state *get_safe_token(device_t *device)
{
	assert(device != NULL);
	assert(device->type() == FD1771 || device->type() == FD1781 ||
		device->type() == FD1791 || device->type() == FD1792 || device->type() == FD1793 || device->type() == FD1794 || device->type() == FD1795 || device->type() == FD1797 ||
		device->type() == FD1761 || device->type() == FD1762 || device->type() == FD1763 || device->type() == FD1764 || device->type() == FD1765 || device->type() == FD1767 ||
		device->type() == WD2791 || device->type() == WD2793 || device->type() == WD2795 || device->type() == WD2797 ||
		device->type() == WD1770 || device->type() == WD1772 || device->type() == WD1773 ||
		device->type() == MB8866 || device->type() == MB8876 || device->type() == MB8877);

	return (wd1770_state *)downcast<wd1770_device *>(device)->token();
}


/***************************************************************************
    HELPER FUNCTIONS
***************************************************************************/

static int wd17xx_has_dal(device_t *device)
{
	return (device->type() == FD1793 || device->type() == FD1794 || device->type() == FD1797 ||
			device->type() == FD1763 || device->type() == FD1764 || device->type() == FD1767 ||
			device->type() == WD1770 || device->type() == WD1772 || device->type() == WD1773 ||
			device->type() == WD2793 || device->type() == WD2797 ||
			device->type() == MB8877);
}

static int wd17xx_is_sd_only(device_t *device)
{
	return (device->type() == FD1771 || device->type() == FD1792 || device->type() == FD1794 || device->type() == FD1762 || device->type() == FD1764);
}

static int wd17xx_has_side_select(device_t *device)
{
	return (device->type() == FD1795 || device->type() == FD1797 ||
			device->type() == FD1765 || device->type() == FD1767 ||
			device->type() == WD2795 || device->type() == WD2797);
}

static int wd17xx_dden(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	if (!w->in_dden_func.isnull())
		return w->in_dden_func();
	else
		return w->dden;
}


/***************************************************************************
    IMPLEMENTATION
***************************************************************************/

/* clear a data request */
static void wd17xx_clear_drq(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	w->status &= ~STA_2_DRQ;

	w->drq = CLEAR_LINE;
	w->out_drq_func(w->drq);
}

/* set data request */
static void wd17xx_set_drq(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	if (w->status & STA_2_DRQ)
		w->status |= STA_2_LOST_DAT;

	w->status |= STA_2_DRQ;

	w->drq = ASSERT_LINE;
	w->out_drq_func(w->drq);
}

/* clear interrupt request */
static void wd17xx_clear_intrq(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	w->intrq = CLEAR_LINE;
	w->out_intrq_func(w->intrq);
}

/* set interrupt request */
static void wd17xx_set_intrq(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	w->status &= ~STA_2_BUSY;
	w->status &= ~STA_2_DRQ;

	w->intrq = ASSERT_LINE;
	w->out_intrq_func(w->intrq);
}

/* set intrq after delay */
static TIMER_CALLBACK( wd17xx_command_callback )
{
	device_t *device = (device_t *)ptr;
	wd1770_state *w = get_safe_token(device);

	if (w->last_command_data != FDC_FORCE_INT)
	{
		wd17xx_set_intrq(device);
	}
}

/* write next byte to data register and set drq */
static TIMER_CALLBACK( wd17xx_data_callback )
{
	device_t *device = (device_t *)ptr;
	wd1770_state *w = get_safe_token(device);

	/* check if this is a write command */
	if( (w->command_type == TYPE_II && w->command == FDC_WRITE_SEC) ||
			(w->command_type == TYPE_III && w->command == FDC_WRITE_TRK) )
	{
		/* we are ready for new data */
		wd17xx_set_drq(device);

		return;
	}

	/* any bytes remaining? */
	if (w->data_count >= 1)
	{
		/* yes */
		w->data = w->buffer[w->data_offset++];

		if (VERBOSE_DATA)
			logerror("wd17xx_data_callback: $%02X (data_count %d)\n", w->data, w->data_count);

		wd17xx_set_drq(device);

		/* any bytes remaining? */
		if (--w->data_count < 1)
		{
			/* no */
			w->data_offset = 0;

			/* clear ddam type */
			w->status &=~STA_2_REC_TYPE;

			/* read a sector with ddam set? */
			if (w->command_type == TYPE_II && w->ddam != 0)
			{
				/* set it */
				w->status |= STA_2_REC_TYPE;
			}

			/* check if we should handle the next sector for a multi record read */
			if (w->command_type == TYPE_II && w->command == FDC_READ_SEC && (w->read_cmd & 0x10))
			{
				if (VERBOSE)
					logerror("wd17xx_data_callback: multi sector read\n");

				if (w->sector == 0xff)
					w->sector = 0x01;
				else
					w->sector++;

				wd17xx_timed_read_sector_request(device);
			}
			else
			{
				/* Delay the INTRQ 3 byte times because we need to read two CRC bytes and
				   compare them with a calculated CRC */
				wd17xx_complete_command(device, DELAY_DATADONE);

				if (VERBOSE)
					logerror("wd17xx_data_callback: data read completed\n");
			}
		}
		else
		{
			/* requeue us for more data */
			w->timer_data->adjust(attotime::from_usec(wd17xx_dden(device) ? 128 : 32));
		}
	}
	else
	{
		logerror("wd17xx_data_callback: (no new data) $%02X (data_count %d)\n", w->data, w->data_count);
	}
}


static void wd17xx_set_busy(device_t *device, attotime duration)
{
	wd1770_state *w = get_safe_token(device);

	w->status |= STA_1_BUSY;

	w->timer_cmd->adjust(duration);
}


/* BUSY COUNT DOESN'T WORK PROPERLY! */

static void wd17xx_command_restore(device_t *device)
{
	wd1770_state *w = get_safe_token(device);
	UINT8 step_counter;

	if (w->drive == NULL)
		return;

	step_counter = 255;

#if 0
	w->status |= STA_1_BUSY;
#endif

	/* setup step direction */
	w->direction = -1;

	w->command_type = TYPE_I;

	/* reset busy count */
	w->busy_count = 0;

	if (1) // image_slotexists(w->drive) : FIXME
	{
		/* keep stepping until track 0 is received or 255 steps have been done */
		while (floppy_tk00_r(w->drive) && (step_counter != 0))
		{
			/* update time to simulate seek time busy signal */
			w->busy_count++;
			floppy_drive_seek(w->drive, w->direction);
			step_counter--;
		}
	}

	/* update track reg */
	w->track = 0;
#if 0
	/* simulate seek time busy signal */
	w->busy_count = 0;  //w->busy_count * ((w->data & FDC_STEP_RATE) + 1);

	/* when command completes set irq */
	wd17xx_set_intrq(device);
#endif
	wd17xx_set_busy(device, attotime::from_usec(100));
}

/*
    Write an entire track. Formats which do not define a write_track
    function pointer will cause a silent return.
    What is written to the image depends on the selected format. Sector
    dumps have to extract the sectors in the track, while track dumps
    may directly write the bytes.
    (The if-part below may thus be removed.)
*/
static void write_track(device_t *device)
{
	wd1770_state *w = get_safe_token(device);
	floppy_image_legacy *floppy;
#if 0
	int i;
	for (i=0;i+4<w->data_offset;)
	{
		if (w->buffer[i]==0xfe)
		{
			/* got address mark */
			int track   = w->buffer[i+1];
			int side    = w->buffer[i+2];
			int sector  = w->buffer[i+3];
			//int len     = w->buffer[i+4];
			int filler  = 0xe5; /* IBM and Thomson */
			int density = w->density;
			floppy_drive_format_sector(w->drive,side,sector,track,
						w->hd,sector,density?1:0,filler);
			i += 128; /* at least... */
		}
		else
			i++;
	}
#endif

	/* Get the size in bytes of the current track. For real hardware this
	may vary per system in small degree, and there even for each track
	and head, so we should not assume a fixed value here.
	As we are using a buffered track writing, we have to find out how long
	the track will become. The only object which can tell us is the
	selected format.
	*/
	w->data_count = 0;
	floppy = flopimg_get_image(w->drive);
	if (floppy != NULL)
		w->data_count = floppy_get_track_size(floppy, w->hd, w->track);

		if (w->data_count==0)
		{
			if (wd17xx_is_sd_only(device))
				w->data_count = TRKSIZE_SD;
			else
				w->data_count = wd17xx_dden(device) ? TRKSIZE_SD : TRKSIZE_DD;
		}

	floppy_drive_write_track_data_info_buffer( w->drive, w->hd, (char *)w->buffer, &(w->data_count) );

	w->data_offset = 0;

	wd17xx_set_drq(device);
	w->status |= STA_2_BUSY;
	w->busy_count = 0;
}

/*
    Read an entire track. It is up to the format to deliver the data. Sector
    dumps may be required to fantasize the missing track bytes, while track
    dumps can directly deliver them.
    (The if-part below may thus be removed.)
*/
static void read_track(device_t *device)
{
	wd1770_state *w = get_safe_token(device);
	floppy_image_legacy *floppy;
#if 0
	UINT8 *psrc;        /* pointer to track format structure */
	UINT8 *pdst;        /* pointer to track buffer */
	int cnt;            /* number of bytes to fill in */
	UINT16 crc;         /* id or data CRC */
	UINT8 d;            /* data */
	UINT8 t = w->track; /* track of DAM */
	UINT8 h = w->head;  /* head of DAM */
	UINT8 s = w->sector_dam;        /* sector of DAM */
	UINT16 l = w->sector_length;    /* sector length of DAM */
	int i;

	for (i = 0; i < w->sec_per_track; i++)
	{
		w->dam_list[i][0] = t;
		w->dam_list[i][1] = h;
		w->dam_list[i][2] = i;
		w->dam_list[i][3] = l >> 8;
	}

	pdst = w->buffer;

	if (w->density)
	{
		psrc = track_DD[0];    /* double density track format */
		cnt = TRKSIZE_DD;
	}
	else
	{
		psrc = track_SD[0];    /* single density track format */
		cnt = TRKSIZE_SD;
	}

	while (cnt > 0)
	{
		if (psrc[0] == 0)      /* no more track format info ? */
		{
			if (w->dam_cnt < w->sec_per_track) /* but more DAM info ? */
			{
				if (w->density)/* DD track ? */
					psrc = track_DD[3];
				else
					psrc = track_SD[3];
			}
		}

		if (psrc[0] != 0)      /* more track format info ? */
		{
			cnt -= psrc[0];    /* subtract size */
			d = psrc[1];

			if (d == 0xf5)     /* clear CRC ? */
			{
				crc = 0xffff;
				d = 0xa1;      /* store A1 */
			}

			for (i = 0; i < *psrc; i++)
				*pdst++ = d;   /* fill data */

			if (d == 0xf7)     /* store CRC ? */
			{
				pdst--;        /* go back one byte */
				*pdst++ = crc & 255;    /* put CRC low */
				*pdst++ = crc / 256;    /* put CRC high */
				cnt -= 1;      /* count one more byte */
			}
			else if (d == 0xfe)/* address mark ? */
			{
				crc = 0xffff;   /* reset CRC */
			}
			else if (d == 0x80)/* sector ID ? */
			{
				pdst--;        /* go back one byte */
				t = *pdst++ = w->dam_list[w->dam_cnt][0]; /* track number */
				h = *pdst++ = w->dam_list[w->dam_cnt][1]; /* head number */
				s = *pdst++ = w->dam_list[w->dam_cnt][2]; /* sector number */
				l = *pdst++ = w->dam_list[w->dam_cnt][3]; /* sector length code */
				w->dam_cnt++;
				crc = ccitt_crc16_one(crc, t);  /* build CRC */
				crc = ccitt_crc16_one(crc, h);  /* build CRC */
				crc = ccitt_crc16_one(crc, s);  /* build CRC */
				crc = ccitt_crc16_one(crc, l);  /* build CRC */
				l = (l == 0) ? 128 : l << 8;
			}
			else if (d == 0xfb)// data address mark ?
			{
				crc = 0xffff;   // reset CRC
			}
			else if (d == 0x81)// sector DATA ?
			{
				pdst--;        /* go back one byte */
				if (seek(w, t, h, s) == 0)
				{
					if (mame_fread(w->image_file, pdst, l) != l)
					{
						w->status = STA_2_CRC_ERR;
						return;
					}
				}
				else
				{
					w->status = STA_2_REC_N_FND;
					return;
				}
				for (i = 0; i < l; i++) // build CRC of all data
					crc = ccitt_crc16_one(crc, *pdst++);
				cnt -= l;
			}
			psrc += 2;
		}
		else
		{
			*pdst++ = 0xff;    /* fill track */
			cnt--;             /* until end */
		}
	}
#endif
	/* Determine the track size. We cannot allow different sizes in this
	design (see above, write_track). */
	w->data_count = 0;
	floppy = flopimg_get_image(w->drive);
	if (floppy != NULL)
		w->data_count = floppy_get_track_size(floppy, w->hd, w->track);

		if (w->data_count==0)
		{
			if (wd17xx_is_sd_only(device))
				w->data_count = TRKSIZE_SD;
			else
				w->data_count = wd17xx_dden(device) ? TRKSIZE_SD : TRKSIZE_DD;
		}

	floppy_drive_read_track_data_info_buffer( w->drive, w->hd, (char *)w->buffer, &(w->data_count) );

	w->data_offset = 0;

	wd17xx_set_drq(device);
	w->status |= STA_2_BUSY;
	w->busy_count = 0;
}


/* read the next data address mark */
static void wd17xx_read_id(device_t *device)
{
	chrn_id id;
	wd1770_state *w = get_safe_token(device);

	w->status &= ~(STA_2_CRC_ERR | STA_2_REC_N_FND);

	/* get next id from disc */
	if (floppy_drive_get_next_id(w->drive, w->hd, &id))
	{
		UINT16 crc = 0xffff;

		w->data_offset = 0;
		w->data_count = 6;

		/* for MFM */
		/* crc includes 3x0x0a1, and 1x0x0fe (id mark) */
		crc = ccitt_crc16_one(crc,0x0a1);
		crc = ccitt_crc16_one(crc,0x0a1);
		crc = ccitt_crc16_one(crc,0x0a1);
		crc = ccitt_crc16_one(crc,0x0fe);

		w->buffer[0] = id.C;
		w->buffer[1] = id.H;
		w->buffer[2] = id.R;
		w->buffer[3] = id.N;
		crc = ccitt_crc16_one(crc, w->buffer[0]);
		crc = ccitt_crc16_one(crc, w->buffer[1]);
		crc = ccitt_crc16_one(crc, w->buffer[2]);
		crc = ccitt_crc16_one(crc, w->buffer[3]);
		/* crc is stored hi-byte followed by lo-byte */
		w->buffer[4] = crc>>8;
		w->buffer[5] = crc & 255;

		w->sector = id.C;

		if (VERBOSE)
			logerror("wd17xx_read_id: read id succeeded.\n");

		wd17xx_timed_data_request(device);
	}
	else
	{
		/* record not found */
		w->status |= STA_2_REC_N_FND;
		//w->sector = w->track;
		if (VERBOSE)
			logerror("wd17xx_read_id: read id failed\n");

		wd17xx_complete_command(device, DELAY_ERROR);
	}
}



void wd17xx_index_pulse_callback(device_t *controller, device_t *img, int state)
{
	wd1770_state *w = get_safe_token(controller);

	if (img != w->drive)
		return;

	w->idx = state;

	if (!state && w->idx && BIT(w->interrupt, 2))
		wd17xx_set_intrq(controller);

	if (w->hld_count)
		w->hld_count--;
}



static int wd17xx_locate_sector(device_t *device)
{
	UINT8 revolution_count;
	chrn_id id;
	wd1770_state *w = get_safe_token(device);

	revolution_count = 0;

	w->status &= ~STA_2_REC_N_FND;

	while (revolution_count!=4)
	{
		if (floppy_drive_get_next_id(w->drive, w->hd, &id))
		{
			/* compare track */
			if (id.C == w->track)
			{
				/* compare head, if we were asked to */
				if (!wd17xx_has_side_select(device) || (id.H == w->head) || (w->head == (UINT8) ~0))
				{
					/* compare id */
					if (id.R == w->sector)
					{
						w->sector_length = 1<<(id.N+7);
						w->sector_data_id = id.data_id;
						/* get ddam status */
						w->ddam = id.flags & ID_FLAG_DELETED_DATA;
						/* got record type here */
						if (VERBOSE)
							logerror("sector found! C:$%02x H:$%02x R:$%02x N:$%02x%s\n", id.C, id.H, id.R, id.N, w->ddam ? " DDAM" : "");
						return 1;
					}
				}
			}
		}

			/* index set? */
		if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_INDEX))
		{
			/* update revolution count */
			revolution_count++;
		}
	}
	return 0;
}


static int wd17xx_find_sector(device_t *device)
{
	wd1770_state *w = get_safe_token(device);
	if ( wd17xx_locate_sector(device) )
	{
		return 1;
	}

	/* record not found */
	w->status |= STA_2_REC_N_FND;

	if (VERBOSE)
		logerror("track %d sector %d not found!\n", w->track, w->sector);

	wd17xx_complete_command(device, DELAY_ERROR);

	return 0;
}

static void wd17xx_side_compare(device_t *device, UINT8 command)
{
	wd1770_state *w = get_safe_token(device);

	if (wd17xx_has_side_select(device))
		wd17xx_set_side(device, (command & FDC_SIDE_CMP_T) ? 1 : 0);

	if (command & FDC_SIDE_CMP_T)
		w->head = (command & FDC_SIDE_CMP_S) ? 1 : 0;
	else
		w->head = ~0;
}

/* read a sector */
static void wd17xx_read_sector(device_t *device)
{
	wd1770_state *w = get_safe_token(device);
	w->data_offset = 0;

	/* side compare? */
	wd17xx_side_compare(device, w->read_cmd);

	if (wd17xx_find_sector(device))
	{
		w->data_count = w->sector_length;

		/* read data */
		floppy_drive_read_sector_data(w->drive, w->hd, w->sector_data_id, (char *)w->buffer, w->sector_length);

		wd17xx_timed_data_request(device);

		w->status |= STA_2_BUSY;
		w->busy_count = 0;
	}
}


/* called on error, or when command is actually completed */
/* KT - I have used a timer for systems that use interrupt driven transfers.
A interrupt occurs after the last byte has been read. If it occurs at the time
when the last byte has been read it causes problems - same byte read again
or bytes missed */
/* TJL - I have add a parameter to allow the emulation to specify the delay
*/
static void wd17xx_complete_command(device_t *device, int delay)
{
	wd1770_state *w = get_safe_token(device);

	w->data_count = 0;

	w->hld_count = 2;

	/* set new timer */
	int usecs = wd17xx_dden(device) ? 32 : 16;
	w->timer_cmd->adjust(attotime::from_usec(usecs));

	/* Kill onshot read/write sector timers */
	w->timer_rs->adjust(attotime::never);
	w->timer_ws->adjust(attotime::never);
}



static void wd17xx_write_sector(device_t *device)
{
	wd1770_state *w = get_safe_token(device);
	/* at this point, the disc is write enabled, and data
	 * has been transfered into our buffer - now write it to
	 * the disc image or to the real disc
	 */

	/* side compare? */
	wd17xx_side_compare(device, w->write_cmd);

	/* find sector */
	if (wd17xx_find_sector(device))
	{
		w->data_count = w->sector_length;

		/* write data */
		floppy_drive_write_sector_data(w->drive, w->hd, w->sector_data_id, (char *)w->buffer, w->sector_length, w->write_cmd & 0x01);
	}
}



/* verify the seek operation by looking for a id that has a matching track value */
static void wd17xx_verify_seek(device_t *device)
{
	UINT8 revolution_count;
	chrn_id id;
	wd1770_state *w = get_safe_token(device);

	revolution_count = 0;

	if (VERBOSE)
		logerror("doing seek verify\n");

	w->status &= ~STA_1_SEEK_ERR;

	/* must be found within 5 revolutions otherwise error */
	while (revolution_count!=5)
	{
		if (floppy_drive_get_next_id(w->drive, w->hd, &id))
		{
			/* compare track */
			if (id.C == w->track)
			{
				if (VERBOSE)
					logerror("seek verify succeeded!\n");
				return;
			}
		}

			/* index set? */
		if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_INDEX))
		{
			/* update revolution count */
			revolution_count++;
		}
	}

	w->status |= STA_1_SEEK_ERR;

	if (VERBOSE)
		logerror("failed seek verify!\n");
}



/* callback to initiate read sector */
static TIMER_CALLBACK( wd17xx_read_sector_callback )
{
	device_t *device = (device_t *)ptr;
	wd1770_state *w = get_safe_token(device);

	/* ok, start that read! */

	if (VERBOSE)
		logerror("wd179x: Read Sector callback.\n");

	if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
		wd17xx_complete_command(device, DELAY_NOTREADY);
	else
		wd17xx_read_sector(device);
}



/* callback to initiate write sector */
static TIMER_CALLBACK( wd17xx_write_sector_callback )
{
	device_t *device = (device_t *)ptr;
	wd1770_state *w = get_safe_token(device);

	/* ok, start that write! */

	if (VERBOSE)
		logerror("wd179x: Write Sector callback.\n");

	if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
		wd17xx_complete_command(device, DELAY_NOTREADY);
	else
	{
		/* drive write protected? */
		if (floppy_wpt_r(w->drive) == CLEAR_LINE)
		{
			w->status |= STA_2_WRITE_PRO;

			wd17xx_complete_command(device, DELAY_ERROR);
		}
		else
		{
			/* side compare? */
			wd17xx_side_compare(device, w->write_cmd);

			/* attempt to find it first before getting data from cpu */
			if (wd17xx_find_sector(device))
			{
				/* request data */
				w->data_offset = 0;
				w->data_count = w->sector_length;

				wd17xx_set_drq(device);

				w->status |= STA_2_BUSY;
				w->busy_count = 0;
			}
		}
	}
}



/* setup a timed data request - data request will be triggered in a few usecs time */
static void wd17xx_timed_data_request(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	/* set new timer */
	w->timer_data->adjust(attotime::from_usec(wd17xx_dden(device) ? 128 : 32));
}



/* setup a timed read sector - read sector will be triggered in a few usecs time */
static void wd17xx_timed_read_sector_request(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	/* set new timer */
	w->timer_rs->adjust(attotime::from_usec(w->pause_time));
}



/* setup a timed write sector - write sector will be triggered in a few usecs time */
static void wd17xx_timed_write_sector_request(device_t *device)
{
	wd1770_state *w = get_safe_token(device);

	/* set new timer */
	w->timer_ws->adjust(attotime::from_usec(w->pause_time));
}


/***************************************************************************
    INTERFACE
***************************************************************************/

/* use this to determine which drive is controlled by WD */
void wd17xx_set_drive(device_t *device, UINT8 drive)
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
		logerror("wd17xx_set_drive: $%02x\n", drive);

	if (w->intf->floppy_drive_tags[drive] != NULL)
	{
		w->drive = device->siblingdevice(w->intf->floppy_drive_tags[drive]);
	}
}

void wd17xx_set_side(device_t *device, UINT8 head)
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
	{
		if (head != w->hd)
			logerror("wd17xx_set_side: $%02x\n", head);
	}

	w->hd = head;
}

void wd17xx_set_pause_time(device_t *device, int usec)
{
	wd1770_state *w = get_safe_token(device);
	w->pause_time = usec;
}


/***************************************************************************
    DEVICE HANDLERS
***************************************************************************/

/* master reset */
WRITE_LINE_DEVICE_HANDLER( wd17xx_mr_w )
{
	wd1770_state *w = get_safe_token(device);

	/* reset device when going from high to low */
	if (w->mr && state == CLEAR_LINE)
	{
		w->command = 0x03;
		w->status &= ~STA_1_NOT_READY; /* ? */
	}

	/* execute restore command when going from low to high */
	if (w->mr == CLEAR_LINE && state)
	{
		wd17xx_command_restore(device);
		w->sector = 0x01;
	}

	w->mr = state;
}

/* ready and enable precomp (1773 only) */
WRITE_LINE_DEVICE_HANDLER( wd17xx_rdy_w )
{
	wd1770_state *w = get_safe_token(device);
	w->rdy = state;
}

/* motor on, 1770 and 1772 only */
READ_LINE_DEVICE_HANDLER( wd17xx_mo_r )
{
	wd1770_state *w = get_safe_token(device);
	return w->mo;
}

/* track zero */
WRITE_LINE_DEVICE_HANDLER( wd17xx_tr00_w )
{
	wd1770_state *w = get_safe_token(device);
	w->tr00 = state;
}

/* index pulse */
WRITE_LINE_DEVICE_HANDLER( wd17xx_idx_w )
{
	wd1770_state *w = get_safe_token(device);
	w->idx = state;

	if (!state && w->idx && BIT(w->interrupt, 2))
		wd17xx_set_intrq(device);
}

/* write protect status */
WRITE_LINE_DEVICE_HANDLER( wd17xx_wprt_w )
{
	wd1770_state *w = get_safe_token(device);
	w->wprt = state;
}

/* double density enable */
WRITE_LINE_DEVICE_HANDLER( wd17xx_dden_w )
{
	wd1770_state *w = get_safe_token(device);

	/* not supported on FD1771, FD1792, FD1794, FD1762 and FD1764 */
	if (wd17xx_is_sd_only(device))
		fatalerror("wd17xx_dden_w: double density input not supported on this model!\n");
	else if (!w->in_dden_func.isnull())
		logerror("wd17xx_dden_w: write has no effect because a read handler is already defined!\n");
	else
		w->dden = state;
}

/* data request */
READ_LINE_DEVICE_HANDLER( wd17xx_drq_r )
{
	wd1770_state *w = get_safe_token(device);
	return w->drq;
}

/* interrupt request */
READ_LINE_DEVICE_HANDLER( wd17xx_intrq_r )
{
	wd1770_state *w = get_safe_token(device);
	return w->intrq;
}

/* read the FDC status register. This clears IRQ line too */
READ8_DEVICE_HANDLER( wd17xx_status_r )
{
	wd1770_state *w = get_safe_token(device);
	int result;

	if (!BIT(w->interrupt, 3))
	{
		wd17xx_clear_intrq(device);
	}

	/* bit 7, 'not ready' or 'motor on' */
	if (device->type() == WD1770 || device->type() == WD1772)
	{
		w->status &= ~STA_1_MOTOR_ON;
		w->status |= w->mo << 7;
	}
	else
	{
		w->status &= ~STA_1_NOT_READY;
		if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
			w->status |= STA_1_NOT_READY;
	}

	result = w->status;

	/* type 1 command or force int command? */
	if ((w->command_type==TYPE_I) || (w->command_type==TYPE_IV && ! w->was_busy))
	{
		result &= ~(STA_1_IPL | STA_1_TRACK0);

		/* bit 1, index pulse */
		result |= w->idx << 1;

		/* bit 2, track 0 state, inverted */
		result |= !floppy_tk00_r(w->drive) << 2;

		if (w->command_type==TYPE_I)
		{
			if (w->hld_count)
				w->status |= STA_1_HD_LOADED;
			else
				w->status &= ~ STA_1_HD_LOADED;
		}

		/* bit 6, write protect, inverted */
		result |= !floppy_wpt_r(w->drive) << 6;
	}

	/* eventually set data request bit */
//  w->status |= w->status_drq;

	if (VERBOSE)
	{
		if (w->data_count < 4)
			logerror("%s: wd17xx_status_r: $%02X (data_count %d)\n", device->machine().describe_context(), result, w->data_count);
	}

	return result ^ (wd17xx_has_dal(device) ? 0 : 0xff);
}

/* read the FDC track register */
READ8_DEVICE_HANDLER( wd17xx_track_r )
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
		logerror("%s: wd17xx_track_r: $%02X\n", device->machine().describe_context(), w->track);

	return w->track ^ (wd17xx_has_dal(device) ? 0 : 0xff);
}

/* read the FDC sector register */
READ8_DEVICE_HANDLER( wd17xx_sector_r )
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
		logerror("%s: wd17xx_sector_r: $%02X\n", device->machine().describe_context(), w->sector);

	return w->sector ^ (wd17xx_has_dal(device) ? 0 : 0xff);
}

/* read the FDC data register */
READ8_DEVICE_HANDLER( wd17xx_data_r )
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE_DATA)
		logerror("%s: wd17xx_data_r: %02x\n", device->machine().describe_context(), w->data);

	/* clear data request */
	wd17xx_clear_drq(device);

	return w->data ^ (wd17xx_has_dal(device) ? 0 : 0xff);
}

/* write the FDC command register */
WRITE8_DEVICE_HANDLER( wd17xx_command_w )
{
	wd1770_state *w = get_safe_token(device);
	if (!wd17xx_has_dal(device)) data ^= 0xff;

	w->last_command_data = data;

	/* only the WD1770 and WD1772 have a 'motor on' line */
	if (device->type() == WD1770 || device->type() == WD1772)
	{
		w->mo = ASSERT_LINE;
		floppy_mon_w(w->drive, CLEAR_LINE);
	}

	floppy_drive_set_ready_state(w->drive, 1,0);

	if (!BIT(w->interrupt, 3))
	{
		wd17xx_clear_intrq(device);
	}

	/* clear write protected. On read sector, read track and read dam, write protected bit is clear */
	w->status &= ~((1<<6) | (1<<5) | (1<<4));

	if ((data & ~FDC_MASK_TYPE_IV) == FDC_FORCE_INT)
	{
		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X FORCE_INT (data_count %d)\n", device->machine().describe_context(), data, w->data_count);

		w->data_count = 0;
		w->data_offset = 0;
		w->was_busy = w->status & STA_2_BUSY;
		w->status &= ~STA_2_BUSY;

		wd17xx_clear_drq(device);

		if (!BIT(w->interrupt, 3) && BIT(data, 3))
		{
			/* set immediate interrupt */
			wd17xx_set_intrq(device);
		}

		if (BIT(w->interrupt, 3))
		{
			if (data == FDC_FORCE_INT)
			{
				/* clear immediate interrupt */
				w->interrupt = data & 0x0f;
			}
			else
			{
				/* keep immediate interrupt */
				w->interrupt = 0x08 | (data & 0x07);
			}
		}
		else
		{
			w->interrupt = data & 0x0f;
		}

		/* terminate command */
		wd17xx_complete_command(device, DELAY_ERROR);

		w->busy_count = 0;
		w->command_type = TYPE_IV;
		return;
	}

	if (data & 0x80)
	{
		/*w->status_ipl = 0;*/

		if ((data & ~FDC_MASK_TYPE_II) == FDC_READ_SEC)
		{
			if (VERBOSE)
			{
				logerror("%s: wd17xx_command_w $%02X READ_SEC (", device->machine().describe_context(), data);
				logerror("cmd=%02X, trk=%02X, sec=%02X, dat=%02X)\n",w->command,w->track,w->sector,w->data);
			}
			w->read_cmd = data;
			w->command = data & ~FDC_MASK_TYPE_II;
			w->command_type = TYPE_II;
			w->status &= ~STA_2_LOST_DAT;
			w->status |= STA_2_BUSY;
			wd17xx_clear_drq(device);

			wd17xx_timed_read_sector_request(device);

			return;
		}

		if ((data & ~FDC_MASK_TYPE_II) == FDC_WRITE_SEC)
		{
			if (VERBOSE)
			{
				logerror("%s: wd17xx_command_w $%02X WRITE_SEC (", device->machine().describe_context(), data);
				logerror("cmd=%02X, trk=%02X, sec=%02X, dat=%02X)\n",w->command,w->track,w->sector,w->data);
			}

			w->write_cmd = data;
			w->command = data & ~FDC_MASK_TYPE_II;
			w->command_type = TYPE_II;
			w->status &= ~STA_2_LOST_DAT;
			w->status |= STA_2_BUSY;
			wd17xx_clear_drq(device);

			wd17xx_timed_write_sector_request(device);

			return;
		}

		if ((data & ~FDC_MASK_TYPE_III) == FDC_READ_TRK)
		{
			if (VERBOSE)
				logerror("%s: wd17xx_command_w $%02X READ_TRK\n", device->machine().describe_context(), data);

			w->command = data & ~FDC_MASK_TYPE_III;
			w->command_type = TYPE_III;
			w->status &= ~STA_2_LOST_DAT;
			wd17xx_clear_drq(device);
#if 1
//          w->status = seek(w, w->track, w->head, w->sector);
			if (w->status == 0)
				read_track(device);
#endif
			return;
		}

		if ((data & ~FDC_MASK_TYPE_III) == FDC_WRITE_TRK)
		{
			if (VERBOSE)
				logerror("%s: wd17xx_command_w $%02X WRITE_TRK\n", device->machine().describe_context(), data);

			w->command_type = TYPE_III;
			w->status &= ~STA_2_LOST_DAT;
			wd17xx_clear_drq(device);

			if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
			{
				wd17xx_complete_command(device, DELAY_NOTREADY);
			}
			else
			{
				/* drive write protected? */
				if (floppy_wpt_r(w->drive) == CLEAR_LINE)
				{
				/* yes */
					w->status |= STA_2_WRITE_PRO;
				/* quit command */
					wd17xx_complete_command(device, DELAY_ERROR);
				}
				else
				{
				w->command = data & ~FDC_MASK_TYPE_III;
				w->data_offset = 0;
				if (wd17xx_is_sd_only(device))
					w->data_count = TRKSIZE_SD;
				else
					w->data_count = wd17xx_dden(device) ? TRKSIZE_SD : TRKSIZE_DD;

				wd17xx_set_drq(device);
				w->status |= STA_2_BUSY;
				w->busy_count = 0;
				}
			}
			return;
		}

		if ((data & ~FDC_MASK_TYPE_III) == FDC_READ_DAM)
		{
			if (VERBOSE)
				logerror("%s: wd17xx_command_w $%02X READ_DAM\n", device->machine().describe_context(), data);

			w->command_type = TYPE_III;
			w->command = data & ~FDC_MASK_TYPE_III;
			w->status &= ~STA_2_LOST_DAT;
			w->status |= STA_2_BUSY;

			wd17xx_clear_drq(device);

			if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
				wd17xx_read_id(device);
			else
				wd17xx_complete_command(device, DELAY_NOTREADY);

			return;
		}

		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X unknown\n", device->machine().describe_context(), data);

		return;
	}

	w->status |= STA_1_BUSY;

	/* clear CRC error */
	w->status &=~STA_1_CRC_ERR;

	if ((data & ~FDC_MASK_TYPE_I) == FDC_RESTORE)
	{
		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X RESTORE\n", device->machine().describe_context(), data);

		wd17xx_command_restore(device);
	}

	if ((data & ~FDC_MASK_TYPE_I) == FDC_SEEK)
	{
		UINT8 newtrack;

		if (VERBOSE)
			logerror("old track: $%02x new track: $%02x\n", w->track, w->data);
		w->command_type = TYPE_I;

		/* setup step direction */
		if (w->track < w->data)
		{
			if (VERBOSE)
				logerror("direction: +1\n");

			w->direction = 1;
		}
		else if (w->track > w->data)
		{
			if (VERBOSE)
				logerror("direction: -1\n");

			w->direction = -1;
		}

		newtrack = w->data;
		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X SEEK (data_reg is $%02X)\n", device->machine().describe_context(), data, newtrack);

		/* reset busy count */
		w->busy_count = 0;

		/* keep stepping until reached track programmed */
		while (w->track != newtrack)
		{
			/* update time to simulate seek time busy signal */
			w->busy_count++;

			/* update track reg */
			w->track += w->direction;

			floppy_drive_seek(w->drive, w->direction);
		}

		/* simulate seek time busy signal */
		w->busy_count = 0;  //w->busy_count * ((data & FDC_STEP_RATE) + 1);
#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, attotime::from_usec(100));

	}

	if ((data & ~(FDC_STEP_UPDATE | FDC_MASK_TYPE_I)) == FDC_STEP)
	{
		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X STEP dir %+d\n", device->machine().describe_context(), data, w->direction);

		w->command_type = TYPE_I;
		/* if it is a real floppy, issue a step command */
		/* simulate seek time busy signal */
		w->busy_count = 0;  //((data & FDC_STEP_RATE) + 1);

		floppy_drive_seek(w->drive, w->direction);

		if (data & FDC_STEP_UPDATE)
			w->track += w->direction;

#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, attotime::from_usec(100));


	}

	if ((data & ~(FDC_STEP_UPDATE | FDC_MASK_TYPE_I)) == FDC_STEP_IN)
	{
		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X STEP_IN\n", device->machine().describe_context(), data);

		w->command_type = TYPE_I;
		w->direction = +1;
		/* simulate seek time busy signal */
		w->busy_count = 0;  //((data & FDC_STEP_RATE) + 1);

		floppy_drive_seek(w->drive, w->direction);

		if (data & FDC_STEP_UPDATE)
			w->track += w->direction;
#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, attotime::from_usec(100));

	}

	if ((data & ~(FDC_STEP_UPDATE | FDC_MASK_TYPE_I)) == FDC_STEP_OUT)
	{
		if (VERBOSE)
			logerror("%s: wd17xx_command_w $%02X STEP_OUT\n", device->machine().describe_context(), data);

		w->command_type = TYPE_I;
		w->direction = -1;
		/* simulate seek time busy signal */
		w->busy_count = 0;  //((data & FDC_STEP_RATE) + 1);

		/* for now only allows a single drive to be selected */
		floppy_drive_seek(w->drive, w->direction);

		if (data & FDC_STEP_UPDATE)
			w->track += w->direction;

#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, attotime::from_usec(100));
	}

	if (w->command_type == TYPE_I)
	{
		/* 0 is enable spin up sequence, 1 is disable spin up sequence */
		if ((data & FDC_STEP_HDLOAD)==0)
		{
			w->status |= STA_1_HD_LOADED;
			w->hld_count = 2;
		}
		else
			w->status &= ~STA_1_HD_LOADED;

		if (data & FDC_STEP_VERIFY)
		{
			/* verify seek */
			wd17xx_verify_seek(device);
		}
	}
}

/* write the FDC track register */
WRITE8_DEVICE_HANDLER( wd17xx_track_w )
{
	wd1770_state *w = get_safe_token(device);
	if (!wd17xx_has_dal(device)) data ^= 0xff;

	w->track = data;

	if (VERBOSE)
		logerror("%s: wd17xx_track_w $%02X\n", device->machine().describe_context(), data);
}

/* write the FDC sector register */
WRITE8_DEVICE_HANDLER( wd17xx_sector_w )
{
	wd1770_state *w = get_safe_token(device);
	if (!wd17xx_has_dal(device)) data ^= 0xff;

	w->sector = data;

	if (VERBOSE)
		logerror("%s: wd17xx_sector_w $%02X\n", device->machine().describe_context(), data);
}

/* write the FDC data register */
WRITE8_DEVICE_HANDLER( wd17xx_data_w )
{
	wd1770_state *w = get_safe_token(device);
	if (!wd17xx_has_dal(device)) data ^= 0xff;

	if (w->data_count > 0)
	{
		wd17xx_clear_drq(device);

		/* put byte into buffer */
		if (VERBOSE_DATA)
			logerror("wd17xx_info buffered data: $%02X at offset %d.\n", data, w->data_offset);

		w->buffer[w->data_offset++] = data;

				if (--w->data_count < 1)
				{
						if (w->command == FDC_WRITE_TRK)
								write_track(device);
						else
								wd17xx_write_sector(device);

						w->data_offset = 0;

						/* Check we should handle the next sector for a multi record write */
						if ( w->command_type == TYPE_II && w->command == FDC_WRITE_SEC && ( w->write_cmd & FDC_MULTI_REC ) )
						{
							w->sector++;
							if (wd17xx_locate_sector(device))
							{
								w->data_count = w->sector_length;

								w->status |= STA_2_BUSY;
								w->busy_count = 0;

								wd17xx_timed_data_request(device);
							}
						}
						else
						{
							wd17xx_complete_command(device, DELAY_DATADONE);
							if (VERBOSE)
								logerror("wd17xx_data_w(): multi data write completed\n");
						}
//                       wd17xx_complete_command(device, DELAY_DATADONE);
				}
				else
				{
						if (w->command == FDC_WRITE_TRK)
						{
								/* Process special data values according to WD17xx specification.
					Note that as CRC values take up two bytes which are written on
					every 0xf7 byte, this will cause the actual image to
					grow larger than what was written from the system. So we need
					to take the value of data_offset when writing the track.
								*/
								if (wd17xx_dden(device))
								{
										switch (data)
										{
										case 0xf5:
										case 0xf6:
												/* not allowed in FM. */
												/* Take back the last write. */
												w->data_offset--;
												break;
										case 0xf7:
												/* Take back the last write. */
												w->data_offset--;
												/* write two crc bytes */
												w->buffer[w->data_offset++] = (w->crc>>8)&0xff;
												w->buffer[w->data_offset++] = (w->crc&0xff);
												w->crc_active = FALSE;
												break;
										case 0xf8:
										case 0xf9:
										case 0xfa:
										case 0xfb:
										case 0xfe:
												/* Preset crc */
												w->crc = 0xffff;
						/* AM is included in the CRC */
						w->crc = ccitt_crc16_one(w->crc, data);
												w->crc_active = TRUE;
												break;
										case 0xfc:
												/* Write index mark. No effect here as we do not store clock patterns.
							Maybe later. */
												break;
										case 0xfd:
												/* Just write, don't use for CRC. */
												break;
										default:
												/* Byte already written. */
												if (w->crc_active)
														w->crc = ccitt_crc16_one(w->crc, data);
										}
								}
								else  /* MFM */
								{
										switch (data)
										{
										case 0xf5:
												/* Take back the last write. */
												w->data_offset--;
												/* Write a1 */
												w->buffer[w->data_offset++] = 0xa1;
												/* Preset CRC */
												w->crc = 0xffff;
												w->crc_active = TRUE;
												break;
										case 0xf6:
												/* Take back the last write. */
												w->data_offset--;
												/* Write c2 */
												w->buffer[w->data_offset++] = 0xc2;
												break;
										case 0xf7:
												/* Take back the last write. */
												w->data_offset--;
												/* write two crc bytes */
												w->buffer[w->data_offset++] = (w->crc>>8)&0xff;
												w->buffer[w->data_offset++] = (w->crc&0xff);
												w->crc_active = FALSE;
												break;
										case 0xf8:
										case 0xf9:
										case 0xfa:
										case 0xfb:
										case 0xfc:
										case 0xfd:
												/* Just write, don't use for CRC. */
												break;
										case 0xfe:
						/* AM is included in the CRC */
						if (w->crc_active)
							w->crc = ccitt_crc16_one(w->crc, data);
												break;
										default:
												/* Byte already written. */
												if (w->crc_active)
														w->crc = ccitt_crc16_one(w->crc, data);
										}
								}
						}
						/* yes... setup a timed data request */
						wd17xx_timed_data_request(device);
				}
	}
	else
	{
		if (VERBOSE)
			logerror("%s: wd17xx_data_w $%02X\n", device->machine().describe_context(), data);
	}
	w->data = data;
}

READ8_DEVICE_HANDLER( wd17xx_r )
{
	UINT8 data = 0;

	switch (offset & 0x03)
	{
	case 0: data = wd17xx_status_r(device, device->machine().driver_data()->generic_space(), 0); break;
	case 1: data = wd17xx_track_r(device, device->machine().driver_data()->generic_space(), 0); break;
	case 2: data = wd17xx_sector_r(device, device->machine().driver_data()->generic_space(), 0); break;
	case 3: data = wd17xx_data_r(device, device->machine().driver_data()->generic_space(), 0); break;
	}

	return data;
}

WRITE8_DEVICE_HANDLER( wd17xx_w )
{
	switch (offset & 0x03)
	{
	case 0: wd17xx_command_w(device, device->machine().driver_data()->generic_space(), 0, data); break;
	case 1: wd17xx_track_w(device, device->machine().driver_data()->generic_space(), 0, data);   break;
	case 2: wd17xx_sector_w(device, device->machine().driver_data()->generic_space(), 0, data);  break;
	case 3: wd17xx_data_w(device, device->machine().driver_data()->generic_space(), 0, data);    break;
	}
}


/***************************************************************************
    MAME DEVICE INTERFACE
***************************************************************************/

static DEVICE_START( wd1770 )
{
	wd1770_state *w = get_safe_token(device);

	assert(device->static_config() != NULL);

	w->intf = (const wd17xx_interface*)device->static_config();

	w->status = STA_1_TRACK0;
	w->pause_time = 1000;

	/* allocate timers */
	w->timer_cmd = device->machine().scheduler().timer_alloc(FUNC(wd17xx_command_callback), (void *)device);
	w->timer_data = device->machine().scheduler().timer_alloc(FUNC(wd17xx_data_callback), (void *)device);
	w->timer_rs = device->machine().scheduler().timer_alloc(FUNC(wd17xx_read_sector_callback), (void *)device);
	w->timer_ws = device->machine().scheduler().timer_alloc(FUNC(wd17xx_write_sector_callback), (void *)device);

	/* resolve callbacks */
	w->in_dden_func.resolve(w->intf->in_dden_func, *device);
	w->out_intrq_func.resolve(w->intf->out_intrq_func, *device);
	w->out_drq_func.resolve(w->intf->out_drq_func, *device);

	/* stepping rate depends on the clock */
	w->stepping_rate[0] = 6;
	w->stepping_rate[1] = 12;
	w->stepping_rate[2] = 20;
	w->stepping_rate[3] = 30;
}

static DEVICE_START( wd1772 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	/* the 1772 has faster track stepping rates */
	w->stepping_rate[0] = 6;
	w->stepping_rate[1] = 12;
	w->stepping_rate[2] = 2;
	w->stepping_rate[3] = 3;
}

static DEVICE_RESET( wd1770 )
{
	wd1770_state *w = get_safe_token(device);
	int i;

	/* set the default state of some input lines */
	w->mr = ASSERT_LINE;
	w->wprt = ASSERT_LINE;
	w->dden = ASSERT_LINE;

	for (i = 0; i < 4; i++)
	{
		if(w->intf->floppy_drive_tags[i]!=NULL) {
			device_t *img = NULL;

			img = device->siblingdevice(w->intf->floppy_drive_tags[i]);

			if (img!=NULL) {
				floppy_drive_set_controller(img,device);
				floppy_drive_set_index_pulse_callback(img, wd17xx_index_pulse_callback);
				floppy_drive_set_rpm( img, 300.);
			}
		}
	}

	wd17xx_set_drive(device, 0);

	w->hd = 0;
	w->hld_count = 0;
	w->sector = 1;
	wd17xx_command_restore(device);
}

void wd17xx_reset(device_t *device)
{
	DEVICE_RESET_CALL( wd1770 );
}

const device_type FD1771 = &device_creator<fd1771_device>;

fd1771_device::fd1771_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1771, "FD1771", tag, owner, clock, "fd1771", __FILE__)
{
}


const device_type FD1781 = &device_creator<fd1781_device>;

fd1781_device::fd1781_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1781, "FD1781", tag, owner, clock, "fd1781", __FILE__)
{
}


const device_type FD1791 = &device_creator<fd1791_device>;

fd1791_device::fd1791_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1791, "FD1791", tag, owner, clock, "fd1791", __FILE__)
{
}


const device_type FD1792 = &device_creator<fd1792_device>;

fd1792_device::fd1792_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1792, "FD1792", tag, owner, clock, "fd1792", __FILE__)
{
}


const device_type FD1793 = &device_creator<fd1793_device>;

fd1793_device::fd1793_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1793, "FD1793", tag, owner, clock, "fd1793", __FILE__)
{
}


const device_type FD1794 = &device_creator<fd1794_device>;

fd1794_device::fd1794_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1794, "FD1794", tag, owner, clock, "fd1794", __FILE__)
{
}


const device_type FD1795 = &device_creator<fd1795_device>;

fd1795_device::fd1795_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1795, "FD1795", tag, owner, clock, "fd1795", __FILE__)
{
}


const device_type FD1797 = &device_creator<fd1797_device>;

fd1797_device::fd1797_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1797, "FD1797", tag, owner, clock, "fd1797", __FILE__)
{
}


const device_type FD1761 = &device_creator<fd1761_device>;

fd1761_device::fd1761_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1761, "FD1761", tag, owner, clock, "fd1761", __FILE__)
{
}


const device_type FD1762 = &device_creator<fd1762_device>;

fd1762_device::fd1762_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1762, "FD1762", tag, owner, clock, "fd1762", __FILE__)
{
}


const device_type FD1763 = &device_creator<fd1763_device>;

fd1763_device::fd1763_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1763, "FD1763", tag, owner, clock, "fd1763", __FILE__)
{
}


const device_type FD1764 = &device_creator<fd1764_device>;

fd1764_device::fd1764_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1764, "FD1764", tag, owner, clock, "fd1764", __FILE__)
{
}


const device_type FD1765 = &device_creator<fd1765_device>;

fd1765_device::fd1765_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1765, "FD1765", tag, owner, clock, "fd1765", __FILE__)
{
}


const device_type FD1767 = &device_creator<fd1767_device>;

fd1767_device::fd1767_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, FD1767, "FD1767", tag, owner, clock, "fd1767", __FILE__)
{
}


const device_type WD2791 = &device_creator<wd2791_device>;

wd2791_device::wd2791_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, WD2791, "WD2791", tag, owner, clock, "wd2791", __FILE__)
{
}


const device_type WD2793 = &device_creator<wd2793_device>;

wd2793_device::wd2793_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, WD2793, "WD2793", tag, owner, clock, "wd2793", __FILE__)
{
}


const device_type WD2795 = &device_creator<wd2795_device>;

wd2795_device::wd2795_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, WD2795, "WD2795", tag, owner, clock, "wd2795", __FILE__)
{
}


const device_type WD2797 = &device_creator<wd2797_device>;

wd2797_device::wd2797_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, WD2797, "WD2797_LEGACY", tag, owner, clock, "wd2797_l", __FILE__)
{
}


const device_type WD1770 = &device_creator<wd1770_device>;

wd1770_device::wd1770_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: device_t(mconfig, WD1770, "WD1770_LEGACY", tag, owner, clock, "wd1770_l", __FILE__)
{
	m_token = global_alloc_clear(wd1770_state);
}
wd1770_device::wd1770_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source)
	: device_t(mconfig, type, name, tag, owner, clock, shortname, source)
{
	m_token = global_alloc_clear(wd1770_state);
}

//-------------------------------------------------
//  device_config_complete - perform any
//  operations now that the configuration is
//  complete
//-------------------------------------------------

void wd1770_device::device_config_complete()
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void wd1770_device::device_start()
{
	DEVICE_START_NAME( wd1770 )(this);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void wd1770_device::device_reset()
{
	DEVICE_RESET_NAME( wd1770 )(this);
}


const device_type WD1772 = &device_creator<wd1772_device>;

wd1772_device::wd1772_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, WD1772, "WD1772_LEGACY", tag, owner, clock, "wd1772_l", __FILE__)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void wd1772_device::device_start()
{
	DEVICE_START_NAME( wd1772 )(this);
}


const device_type WD1773 = &device_creator<wd1773_device>;

wd1773_device::wd1773_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, WD1773, "WD1773_LEGACY", tag, owner, clock, "wd1773_l", __FILE__)
{
}


const device_type MB8866 = &device_creator<mb8866_device>;

mb8866_device::mb8866_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, MB8866, "MB8866", tag, owner, clock, "mb8866", __FILE__)
{
}


const device_type MB8876 = &device_creator<mb8876_device>;

mb8876_device::mb8876_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, MB8876, "MB8876", tag, owner, clock, "mb8876", __FILE__)
{
}


const device_type MB8877 = &device_creator<mb8877_device>;

mb8877_device::mb8877_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: wd1770_device(mconfig, MB8877, "MB8877_LEGACY", tag, owner, clock, "mb8877_l", __FILE__)
{
}
