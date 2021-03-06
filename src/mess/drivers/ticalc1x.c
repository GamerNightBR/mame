// license:BSD-3-Clause
// copyright-holders:hap, Sean Riddle
/***************************************************************************

  Texas Instruments TMS1xxx/0970/0980 handheld calculators (mostly single-chip)

  Refer to their official manuals on how to use them.


  TODO:
  - MCU clocks are unknown

***************************************************************************/

#include "emu.h"
#include "cpu/tms0980/tms0980.h"

#include "ti1270.lh"
#include "ti30.lh"
#include "tisr16.lh"
#include "wizatron.lh"


class ticalc1x_state : public driver_device
{
public:
	ticalc1x_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_button_matrix(*this, "IN")
	{ }

	required_device<cpu_device> m_maincpu;
	optional_ioport_array<11> m_button_matrix; // up to 11 rows

	UINT16 m_r;
	UINT16 m_o;
	bool m_power_on;

	UINT16 m_display_state[0x10];
	UINT16 m_display_cache[0x10];
	UINT8 m_display_decay[0x100];

	DECLARE_READ8_MEMBER(tisr16_read_k);
	DECLARE_WRITE16_MEMBER(tisr16_write_o);
	DECLARE_WRITE16_MEMBER(tisr16_write_r);
	void tisr16_display_update();

	DECLARE_READ8_MEMBER(ti1270_read_k);
	DECLARE_WRITE16_MEMBER(ti1270_write_o);
	DECLARE_WRITE16_MEMBER(ti1270_write_r);

	DECLARE_READ8_MEMBER(wizatron_read_k);
	DECLARE_WRITE16_MEMBER(wizatron_write_o);
	DECLARE_WRITE16_MEMBER(wizatron_write_r);

	DECLARE_READ8_MEMBER(ti30_read_k);
	DECLARE_WRITE16_MEMBER(ti30_write_o);
	DECLARE_WRITE16_MEMBER(ti30_write_r);

	DECLARE_INPUT_CHANGED_MEMBER(power_button);
	DECLARE_WRITE_LINE_MEMBER(auto_power_off);

	TIMER_DEVICE_CALLBACK_MEMBER(display_decay_tick);
	void display_update();

	virtual void machine_reset();
	virtual void machine_start();
};



/***************************************************************************

  LED Display

***************************************************************************/

// Devices with TMS09x0 strobe the outputs very fast, it is unnoticeable to the user.
// To prevent flickering here, we need to simulate a decay.

// decay time, in steps of 1ms
#define DISPLAY_DECAY_TIME 50

void ticalc1x_state::display_update()
{
	UINT16 active_state[0x10];

	for (int i = 0; i < 0x10; i++)
	{
		active_state[i] = 0;

		for (int j = 0; j < 0x10; j++)
		{
			int di = j << 4 | i;

			// turn on powered segments
			if (m_power_on && m_display_state[i] >> j & 1)
				m_display_decay[di] = DISPLAY_DECAY_TIME;

			// determine active state
			int ds = (m_display_decay[di] != 0) ? 1 : 0;
			active_state[i] |= (ds << j);
		}
	}

	// on difference, send to output
	for (int i = 0; i < 0x10; i++)
		if (m_display_cache[i] != active_state[i])
		{
			output_set_digit_value(i, active_state[i]);

			for (int j = 0; j < 8; j++)
				output_set_lamp_value(i*10 + j, active_state[i] >> j & 1);
		}

	memcpy(m_display_cache, active_state, sizeof(m_display_cache));
}

TIMER_DEVICE_CALLBACK_MEMBER(ticalc1x_state::display_decay_tick)
{
	// slowly turn off unpowered segments
	for (int i = 0; i < 0x100; i++)
		if (!(m_display_state[i & 0xf] >> (i>>4) & 1) && m_display_decay[i])
			m_display_decay[i]--;

	display_update();
}



/***************************************************************************

  I/O

***************************************************************************/

// SR-16: TMS1000 MCU labeled TMS1001NL. die labeled 1001A

void ticalc1x_state::tisr16_display_update()
{
	// update leds state
	for (int i = 0; i < 11; i++)
		if (m_r >> i & 1)
			m_display_state[i] = m_o;

	// exponent sign (not 100% sure this is correct)
	m_display_state[11] = (m_display_state[0] | m_display_state[1]) ? 0x40 : 0;

	// send to output
	for (int i = 0; i < 12; i++)
		output_set_digit_value(i, m_display_state[i]);
}

READ8_MEMBER(ticalc1x_state::tisr16_read_k)
{
	UINT8 k = 0;

	// read selected button rows
	for (int i = 0; i < 11; i++)
		if (m_r >> i & 1)
			k |= m_button_matrix[i]->read();

	return k;
}

WRITE16_MEMBER(ticalc1x_state::tisr16_write_r)
{
	// R0-R10: input mux
	// R0-R10: select digit (right-to-left)
	m_r = data;

	tisr16_display_update();
}

WRITE16_MEMBER(ticalc1x_state::tisr16_write_o)
{
	// O0-O7: digit segments
	m_o = data;

	tisr16_display_update();
}


// TI-1270: TMS0970 MCU labeled TMC0974NL ZA0355, DP0974A. die labeled 0970D-74A

READ8_MEMBER(ticalc1x_state::ti1270_read_k)
{
	UINT8 k = 0;

	// read selected button rows
	for (int i = 0; i < 7; i++)
		if (m_o >> (i+1) & 1)
			k |= m_button_matrix[i]->read();

	return k;
}

WRITE16_MEMBER(ticalc1x_state::ti1270_write_r)
{
	// R0-R7: select digit (right-to-left)
	for (int i = 0; i < 8; i++)
		m_display_state[i] = (data >> i & 1) ? m_o : 0;

	display_update();
}

WRITE16_MEMBER(ticalc1x_state::ti1270_write_o)
{
	// O1-O5,O7: input mux
	// O0-O7: digit segments
	m_o = data;
}


// WIZ-A-TRON (educational toy): TMS0970 MCU labeled TMC0907NL ZA0379, DP0907BS. die labeled 0970F-07B

READ8_MEMBER(ticalc1x_state::wizatron_read_k)
{
	UINT8 k = 0;

	// read selected button rows
	for (int i = 0; i < 4; i++)
		if (m_o >> (i+1) & 1)
			k |= m_button_matrix[i]->read();

	return k;
}

WRITE16_MEMBER(ticalc1x_state::wizatron_write_r)
{
	// R0-R8: select digit (right-to-left)
	// note: 3rd digit is custom(not 7seg), for math symbols
	for (int i = 0; i < 9; i++)
		m_display_state[i] = (data >> i & 1) ? m_o : 0;

	// 6th digit only has A and G for =
	m_display_state[3] &= 0x41;

	display_update();
}

WRITE16_MEMBER(ticalc1x_state::wizatron_write_o)
{
	// O1-O4: input mux
	// O0-O6: digit segments A-G
	// O7: N/C
	m_o = data & 0x7f;
}


// TI-30: TMS0980 MCU labeled TMC0981NL. die labeled 0980B-81F
// TI Programmer: TMS0980 MCU labeled ZA0675NL, JP0983AT. die labeled 0980B-83
// TI Business Analyst-I: TMS0980 MCU labeled TMC0982NL. die labeled 0980B-82F

READ8_MEMBER(ticalc1x_state::ti30_read_k)
{
	// the Vss row is always on
	UINT8 k = m_button_matrix[8]->read();

	// read selected button rows
	for (int i = 0; i < 8; i++)
		if (m_o >> i & 1)
			k |= m_button_matrix[i]->read();

	return k;
}

WRITE16_MEMBER(ticalc1x_state::ti30_write_r)
{
	// R0-R8: select digit
	UINT8 o = BITSWAP8(m_o,7,5,2,1,4,0,6,3);
	for (int i = 0; i < 9; i++)
		m_display_state[i] = (data >> i & 1) ? o : 0;

	// 1st digit only has segments B,F,G,DP
	m_display_state[0] &= 0xe2;

	display_update();
}

WRITE16_MEMBER(ticalc1x_state::ti30_write_o)
{
	// O1-O5,O7: input mux
	// O0-O7: digit segments
	m_o = data;
}



/***************************************************************************

  Inputs

***************************************************************************/

INPUT_CHANGED_MEMBER(ticalc1x_state::power_button)
{
	m_power_on = (bool)(FPTR)param;
	m_maincpu->set_input_line(INPUT_LINE_RESET, m_power_on ? CLEAR_LINE : ASSERT_LINE);
}

static INPUT_PORTS_START( tisr16 )
	PORT_START("IN.0") // R0
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS_PAD) PORT_NAME("-")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_END) PORT_NAME("RCL")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0")

	PORT_START("IN.1") // R1
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PLUS_PAD) PORT_NAME("+")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_BACKSPACE) PORT_NAME("CE")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1")

	PORT_START("IN.2") // R2
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ASTERISK) PORT_NAME(UTF8_MULTIPLY)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_NAME("+/-")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2")

	PORT_START("IN.3") // R3
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH_PAD) PORT_NAME(UTF8_DIVIDE)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CODE(KEYCODE_DEL_PAD) PORT_NAME(".")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3")

	PORT_START("IN.4") // R4
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("=")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_TILDE) PORT_NAME("EE")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4")

	PORT_START("IN.5") // R5
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_EQUALS) PORT_NAME(UTF8_CAPITAL_SIGMA)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_HOME) PORT_NAME("STO")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5")

	PORT_START("IN.6") // R6
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_NAME("1/x")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Y) PORT_NAME("y" UTF8_POW_X)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6")

	PORT_START("IN.7") // R7
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_NAME("x" UTF8_POW_2)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7")

	PORT_START("IN.8") // R8
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_T) PORT_NAME("10" UTF8_POW_X)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_NAME("e" UTF8_POW_X)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8")

	PORT_START("IN.9") // R9
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_R) PORT_NAME(UTF8_SQUAREROOT"x")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9")

	PORT_START("IN.10") // R10
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_DEL) PORT_NAME("C")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_O) PORT_NAME("log")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_L) PORT_NAME("ln(x)")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END


static INPUT_PORTS_START( ti1270 )
	PORT_START("IN.0") // O1
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_BACKSPACE) PORT_CODE(KEYCODE_DEL) PORT_NAME("CE/C")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CODE(KEYCODE_DEL_PAD) PORT_NAME(".")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("=")

	PORT_START("IN.1") // O2
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PLUS_PAD) PORT_NAME("+")

	PORT_START("IN.2") // O3
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS_PAD) PORT_NAME("-")

	PORT_START("IN.3") // O4
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ASTERISK) PORT_NAME(UTF8_MULTIPLY)

	PORT_START("IN.4") // O5
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_HOME) PORT_NAME("STO")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_END) PORT_NAME("RCL")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_P) PORT_CODE(KEYCODE_I) PORT_NAME(UTF8_SMALL_PI)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH_PAD) PORT_NAME(UTF8_DIVIDE)

	PORT_START("IN.5") // O6
	PORT_BIT( 0x0f, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("IN.6") // O7
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_NAME("1/x")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_NAME("x" UTF8_POW_2)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_R) PORT_NAME(UTF8_SQUAREROOT"x")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_NAME("+/-")
INPUT_PORTS_END


static INPUT_PORTS_START( wizatron )
	PORT_START("IN.0") // O1
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_DEL) PORT_CODE(KEYCODE_DEL_PAD) PORT_NAME("CLEAR")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("=")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PLUS_PAD) PORT_NAME("+")

	PORT_START("IN.1") // O2
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS_PAD) PORT_NAME("-")

	PORT_START("IN.2") // O3
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ASTERISK) PORT_NAME(UTF8_MULTIPLY)

	PORT_START("IN.3") // O4
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH_PAD) PORT_NAME(UTF8_DIVIDE)
INPUT_PORTS_END


static INPUT_PORTS_START( ti30 )
	PORT_START("IN.0") // O0
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Y) PORT_NAME("y" UTF8_POW_X)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_K) PORT_NAME("K")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_O) PORT_NAME("log")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_TILDE) PORT_NAME("EE" UTF8_DOWN)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_L) PORT_NAME("ln(x)")

	PORT_START("IN.1") // O1
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ASTERISK) PORT_NAME(UTF8_MULTIPLY)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_HOME) PORT_NAME("STO")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9")

	PORT_START("IN.2") // O2
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS_PAD) PORT_NAME("-")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_END) PORT_NAME("RCL")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6")

	PORT_START("IN.3") // O3
	PORT_BIT( 0x1f, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("IN.4") // O4
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH_PAD) PORT_NAME(UTF8_DIVIDE)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_P) PORT_NAME(UTF8_SMALL_PI)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_OPENBRACE) PORT_NAME("(")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH) PORT_NAME("%")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_NAME(")")

	PORT_START("IN.5") // O5
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PLUS_PAD) PORT_NAME("+")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_EQUALS) PORT_NAME("SUM")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3")

	PORT_START("IN.6") // O6
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_D) PORT_NAME("DRG")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_I) PORT_NAME("INV")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_C) PORT_NAME("cos")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_S) PORT_NAME("sin")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_T) PORT_NAME("tan")

	PORT_START("IN.7") // O7
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("=")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_NAME("EXC")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CODE(KEYCODE_DEL_PAD) PORT_NAME(".")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_NAME("+/-")

	// note: even though power buttons are on the matrix, they are not CPU-controlled
	PORT_START("IN.8") // Vss!
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PGUP) PORT_CODE(KEYCODE_DEL) PORT_NAME("ON/C") PORT_CHANGED_MEMBER(DEVICE_SELF, ticalc1x_state, power_button, (void *)true)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_NAME("1/x")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_R) PORT_NAME(UTF8_SQUAREROOT"x")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_NAME("x" UTF8_POW_2)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PGDN) PORT_NAME("OFF") PORT_CHANGED_MEMBER(DEVICE_SELF, ticalc1x_state, power_button, (void *)false)
INPUT_PORTS_END


static INPUT_PORTS_START( tiprog )
	PORT_START("IN.0") // O0
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_K) PORT_NAME("K")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_NAME("SHF")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_NAME("E")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_D) PORT_NAME("d")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F) PORT_NAME("F")

	PORT_START("IN.1") // O1
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ASTERISK) PORT_NAME(UTF8_MULTIPLY)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_O) PORT_NAME("OR")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9")

	PORT_START("IN.2") // O2
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS_PAD) PORT_NAME("-")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_N) PORT_NAME("AND")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6")

	PORT_START("IN.3") // O3
	PORT_BIT( 0x1f, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("IN.4") // O4
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH_PAD) PORT_NAME(UTF8_DIVIDE)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_TILDE) PORT_NAME("1'sC")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_B) PORT_NAME("b")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_A) PORT_NAME("A")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_C) PORT_NAME("C")

	PORT_START("IN.5") // O5
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PLUS_PAD) PORT_NAME("+")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_NAME("XOR")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3")

	PORT_START("IN.6") // O6
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_NAME(")")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_HOME) PORT_NAME("STO")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_EQUALS) PORT_NAME("SUM")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_END) PORT_NAME("RCL")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_OPENBRACE) PORT_NAME("(")

	PORT_START("IN.7") // O7
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("=")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_BACKSPACE) PORT_NAME("CE")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CODE(KEYCODE_DEL_PAD) PORT_NAME(".")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_NAME("+/-")

	// note: even though power buttons are on the matrix, they are not CPU-controlled
	PORT_START("IN.8") // Vss!
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_DEL) PORT_CODE(KEYCODE_PGUP) PORT_NAME("C/ON") PORT_CHANGED_MEMBER(DEVICE_SELF, ticalc1x_state, power_button, (void *)true)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_G) PORT_NAME("DEC")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_J) PORT_NAME("OCT")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_H) PORT_NAME("HEX")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PGDN) PORT_NAME("OFF") PORT_CHANGED_MEMBER(DEVICE_SELF, ticalc1x_state, power_button, (void *)false)
INPUT_PORTS_END


static INPUT_PORTS_START( tibusan1 )
	// PORT_NAME lists functions under [2nd] as secondaries.
	PORT_START("IN.0") // O0
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Y) PORT_NAME("y" UTF8_POW_X"  " UTF8_POW_X"" UTF8_SQUAREROOT"y") // 2nd one implies xth root of y
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH) PORT_NAME("%  " UTF8_CAPITAL_DELTA"%")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_S) PORT_NAME("SEL")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_C) PORT_NAME("CST")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_M) PORT_NAME("MAR")

	PORT_START("IN.1") // O1
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ASTERISK) PORT_NAME(UTF8_MULTIPLY)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_HOME) PORT_NAME("STO  m")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9")

	PORT_START("IN.2") // O2
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS_PAD) PORT_NAME("-")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_END) PORT_NAME("RCL  b")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6")

	PORT_START("IN.3") // O3
	PORT_BIT( 0x1f, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("IN.4") // O4
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_SLASH_PAD) PORT_NAME(UTF8_DIVIDE)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_EQUALS) PORT_NAME(UTF8_CAPITAL_SIGMA"+  " UTF8_CAPITAL_SIGMA"-")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_OPENBRACE) PORT_NAME("(  AN-CI\"")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_COMMA) PORT_NAME("x<>y  L.R.")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_NAME(")  1/x")

	PORT_START("IN.5") // O5
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PLUS_PAD) PORT_NAME("+")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_X) PORT_NAME("SUM  x" UTF8_PRIME)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3")

	PORT_START("IN.6") // O6
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_F) PORT_NAME("FV")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_N) PORT_NAME("N")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_P) PORT_NAME("PMT")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_I) PORT_NAME("%i")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_V) PORT_NAME("PV")

	PORT_START("IN.7") // O7
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("=")
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_E) PORT_NAME("EXC  x" UTF8_PRIME)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_STOP) PORT_CODE(KEYCODE_DEL_PAD) PORT_NAME(".")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0")
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_MINUS) PORT_NAME("+/-")

	// note: even though power buttons are on the matrix, they are not CPU-controlled
	PORT_START("IN.8") // Vss!
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PGUP) PORT_CODE(KEYCODE_DEL) PORT_NAME("ON/C") PORT_CHANGED_MEMBER(DEVICE_SELF, ticalc1x_state, power_button, (void *)true)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_NAME("2nd")
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_Q) PORT_NAME("x" UTF8_POW_2"  " UTF8_SQUAREROOT"x")
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_L) PORT_NAME("ln(x)  e" UTF8_POW_X)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD ) PORT_CODE(KEYCODE_PGDN) PORT_NAME("OFF") PORT_CHANGED_MEMBER(DEVICE_SELF, ticalc1x_state, power_button, (void *)false)
INPUT_PORTS_END



/***************************************************************************

  Machine Config(s)

***************************************************************************/

WRITE_LINE_MEMBER(ticalc1x_state::auto_power_off)
{
	// TMS0980 auto power-off opcode
	if (state)
	{
		m_power_on = false;
		m_maincpu->set_input_line(INPUT_LINE_RESET, ASSERT_LINE);
	}
}


void ticalc1x_state::machine_reset()
{
	m_power_on = true;
}

void ticalc1x_state::machine_start()
{
	// zerofill
	memset(m_display_state, 0, sizeof(m_display_state));
	memset(m_display_cache, 0, sizeof(m_display_cache));
	memset(m_display_decay, 0, sizeof(m_display_decay));

	m_r = 0;
	m_o = 0;
	m_power_on = false;

	// register for savestates
	save_item(NAME(m_display_state));
	save_item(NAME(m_display_cache));
	save_item(NAME(m_display_decay));

	save_item(NAME(m_r));
	save_item(NAME(m_o));
	save_item(NAME(m_power_on));
}


static MACHINE_CONFIG_START( tisr16, ticalc1x_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", TMS1000, 250000) // guessed
	MCFG_TMS1XXX_READ_K_CB(READ8(ticalc1x_state, tisr16_read_k))
	MCFG_TMS1XXX_WRITE_O_CB(WRITE16(ticalc1x_state, tisr16_write_o))
	MCFG_TMS1XXX_WRITE_R_CB(WRITE16(ticalc1x_state, tisr16_write_r))

	MCFG_DEFAULT_LAYOUT(layout_tisr16)
MACHINE_CONFIG_END


static MACHINE_CONFIG_START( t9base, ticalc1x_state )

	/* basic machine hardware */
	MCFG_TIMER_DRIVER_ADD_PERIODIC("display_decay", ticalc1x_state, display_decay_tick, attotime::from_msec(1))

	/* no video! */

	/* no sound! */
MACHINE_CONFIG_END

static MACHINE_CONFIG_DERIVED( ti1270, t9base )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", TMS0970, 250000) // guessed
	MCFG_TMS1XXX_READ_K_CB(READ8(ticalc1x_state, ti1270_read_k))
	MCFG_TMS1XXX_WRITE_O_CB(WRITE16(ticalc1x_state, ti1270_write_o))
	MCFG_TMS1XXX_WRITE_R_CB(WRITE16(ticalc1x_state, ti1270_write_r))

	MCFG_DEFAULT_LAYOUT(layout_ti1270)
MACHINE_CONFIG_END

static MACHINE_CONFIG_DERIVED( wizatron, t9base )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", TMS0970, 250000) // guessed
	MCFG_TMS1XXX_READ_K_CB(READ8(ticalc1x_state, wizatron_read_k))
	MCFG_TMS1XXX_WRITE_O_CB(WRITE16(ticalc1x_state, wizatron_write_o))
	MCFG_TMS1XXX_WRITE_R_CB(WRITE16(ticalc1x_state, wizatron_write_r))

	MCFG_DEFAULT_LAYOUT(layout_wizatron)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( ti30, t9base )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", TMS0980, 400000) // guessed
	MCFG_TMS1XXX_READ_K_CB(READ8(ticalc1x_state, ti30_read_k))
	MCFG_TMS1XXX_WRITE_O_CB(WRITE16(ticalc1x_state, ti30_write_o))
	MCFG_TMS1XXX_WRITE_R_CB(WRITE16(ticalc1x_state, ti30_write_r))
	MCFG_TMS1XXX_POWER_OFF_CB(WRITELINE(ticalc1x_state, auto_power_off))

	MCFG_DEFAULT_LAYOUT(layout_ti30)
MACHINE_CONFIG_END



/***************************************************************************

  Game driver(s)

***************************************************************************/

ROM_START( tisr16 )
	ROM_REGION( 0x0400, "maincpu", 0 )
	ROM_LOAD( "tms1001nl", 0x0000, 0x0400, CRC(b7ce3c1d) SHA1(95cdb0c6be31043f4fe06314ed41c0ca1337bc46) )

	ROM_REGION( 867, "maincpu:mpla", 0 )
	ROM_LOAD( "tms1000_sr16_mpla.pla", 0, 867, CRC(5b35019c) SHA1(730d3b9041ed76d57fbedd73b009477fe432b386) )
	ROM_REGION( 365, "maincpu:opla", 0 )
	ROM_LOAD( "tms1000_sr16_opla.pla", 0, 365, CRC(29b08739) SHA1(d55f01e40a2d493d45ea422f12e63b01bcde08fb) )
ROM_END

ROM_START( ti1270 )
	ROM_REGION( 0x0400, "maincpu", 0 )
	ROM_LOAD( "tms0974nl", 0x0000, 0x0400, CRC(48e09b4b) SHA1(17f27167164df223f9f06082ece4c3fc3900eda3) )

	ROM_REGION( 782, "maincpu:ipla", 0 )
	ROM_LOAD( "tms0970_ti1270_ipla.pla", 0, 782, CRC(05306ef8) SHA1(60a0a3c49ce330bce0c27f15f81d61461d0432ce) )
	ROM_REGION( 860, "maincpu:mpla", 0 )
	ROM_LOAD( "tms0970_ti1270_mpla.pla", 0, 860, CRC(6ff5d51d) SHA1(59d3e5de290ba57694068ddba78d21a0c1edf427) )
	ROM_REGION( 352, "maincpu:opla", 0 )
	ROM_LOAD( "tms0970_ti1270_opla.pla", 0, 352, CRC(f39bf0a4) SHA1(160341490043eb369720d5f487cf0f59f458a93e) )
	ROM_REGION( 157, "maincpu:spla", 0 )
	ROM_LOAD( "tms0970_ti1270_spla.pla", 0, 157, CRC(56c37a4f) SHA1(18ecc20d2666e89673739056483aed5a261ae927) )
ROM_END

ROM_START( wizatron )
	ROM_REGION( 0x0400, "maincpu", 0 )
	ROM_LOAD( "dp0907bs", 0x0000, 0x0400, CRC(5a6af094) SHA1(b1f27e1f13f4db3b052dd50fb08dbf9c4d8db26e) )

	ROM_REGION( 782, "maincpu:ipla", 0 )
	ROM_LOAD( "tms0970_wizatron_ipla.pla", 0, 782, CRC(05306ef8) SHA1(60a0a3c49ce330bce0c27f15f81d61461d0432ce) )
	ROM_REGION( 860, "maincpu:mpla", 0 )
	ROM_LOAD( "tms0970_wizatron_mpla.pla", 0, 860, CRC(7f50ab2e) SHA1(bff3be9af0e322986f6e545b567c97d70e135c93) )
	ROM_REGION( 352, "maincpu:opla", 0 )
	ROM_LOAD( "tms0970_wizatron_opla.pla", 0, 352, CRC(745a3900) SHA1(031b55a0cf783c8a88eec4289d4373eb8538f374) )
	ROM_REGION( 157, "maincpu:spla", 0 )
	ROM_LOAD( "tms0970_wizatron_spla.pla", 0, 157, CRC(56c37a4f) SHA1(18ecc20d2666e89673739056483aed5a261ae927) )
ROM_END

ROM_START( ti30 )
	ROM_REGION( 0x1000, "maincpu", 0 )
	ROM_LOAD16_WORD( "tmc0981nl", 0x0000, 0x1000, CRC(41298a14) SHA1(06f654c70add4044a612d3a38b0c2831c188fd0c) )

	ROM_REGION( 1246, "maincpu:ipla", 0 )
	ROM_LOAD( "tms0980_default_ipla.pla", 0, 1246, CRC(42db9a38) SHA1(2d127d98028ec8ec6ea10c179c25e447b14ba4d0) )
	ROM_REGION( 1982, "maincpu:mpla", 0 )
	ROM_LOAD( "tms0980_default_mpla.pla", 0, 1982, CRC(3709014f) SHA1(d28ee59ded7f3b9dc3f0594a32a98391b6e9c961) )
	ROM_REGION( 352, "maincpu:opla", 0 )
	ROM_LOAD( "tms0980_ti30_opla.pla", 0, 352, CRC(38788410) SHA1(cb3d1a61190b887cd2e6d9c60b4fdb9b901f7eed) )
	ROM_REGION( 157, "maincpu:spla", 0 )
	ROM_LOAD( "tms0980_ti30_spla.pla", 0, 157, CRC(399aa481) SHA1(72c56c58fde3fbb657d69647a9543b5f8fc74279) )
ROM_END

ROM_START( tibusan1 )
	ROM_REGION( 0x1000, "maincpu", 0 )
	ROM_LOAD16_WORD( "tmc0982nl", 0x0000, 0x1000, CRC(6954560a) SHA1(6c153a0c9239a811e3514a43d809964c06f8f88e) )

	ROM_REGION( 1246, "maincpu:ipla", 0 )
	ROM_LOAD( "tms0980_default_ipla.pla", 0, 1246, CRC(42db9a38) SHA1(2d127d98028ec8ec6ea10c179c25e447b14ba4d0) )
	ROM_REGION( 1982, "maincpu:mpla", 0 )
	ROM_LOAD( "tms0980_default_mpla.pla", 0, 1982, CRC(3709014f) SHA1(d28ee59ded7f3b9dc3f0594a32a98391b6e9c961) )
	ROM_REGION( 352, "maincpu:opla", 0 )
	ROM_LOAD( "tms0980_tibusan1_opla.pla", 0, 352, CRC(38788410) SHA1(cb3d1a61190b887cd2e6d9c60b4fdb9b901f7eed) )
	ROM_REGION( 157, "maincpu:spla", 0 )
	ROM_LOAD( "tms0980_tibusan1_spla.pla", 0, 157, CRC(399aa481) SHA1(72c56c58fde3fbb657d69647a9543b5f8fc74279) )
ROM_END

ROM_START( tiprog )
	ROM_REGION( 0x1000, "maincpu", 0 )
	ROM_LOAD16_WORD( "za0675nl", 0x0000, 0x1000, CRC(82355854) SHA1(03fab373bce04df8ea3fe25352525e8539213626) )

	ROM_REGION( 1246, "maincpu:ipla", 0 )
	ROM_LOAD( "tms0980_default_ipla.pla", 0, 1246, CRC(42db9a38) SHA1(2d127d98028ec8ec6ea10c179c25e447b14ba4d0) )
	ROM_REGION( 1982, "maincpu:mpla", 0 )
	ROM_LOAD( "tms0980_tiprog_mpla.pla", 0, 1982, CRC(57043284) SHA1(0fa06d5865830ecdb3d870271cb92ac917bed3ca) )
	ROM_REGION( 352, "maincpu:opla", 0 )
	ROM_LOAD( "tms0980_tiprog_opla.pla", 0, 352, BAD_DUMP CRC(2a63956f) SHA1(26a62ca2b5973d8564e580e12230292f6d2888d9) ) // corrected by hand
	ROM_REGION( 157, "maincpu:spla", 0 )
	ROM_LOAD( "tms0980_tiprog_spla.pla", 0, 157, CRC(399aa481) SHA1(72c56c58fde3fbb657d69647a9543b5f8fc74279) )
ROM_END



COMP( 1974, tisr16,   0, 0, tisr16,   tisr16,   driver_device, 0, "Texas Instruments", "SR-16 (Texas Instruments)", GAME_SUPPORTS_SAVE | GAME_NO_SOUND_HW )

COMP( 1976, ti1270,   0, 0, ti1270,   ti1270,   driver_device, 0, "Texas Instruments", "TI-1270", GAME_SUPPORTS_SAVE | GAME_NO_SOUND_HW )
COMP( 1977, wizatron, 0, 0, wizatron, wizatron, driver_device, 0, "Texas Instruments", "Wiz-A-Tron", GAME_SUPPORTS_SAVE | GAME_NO_SOUND_HW )

COMP( 1976, ti30,     0, 0, ti30,     ti30,     driver_device, 0, "Texas Instruments", "TI-30", GAME_SUPPORTS_SAVE | GAME_NO_SOUND_HW )
COMP( 1977, tiprog,   0, 0, ti30,     tiprog,   driver_device, 0, "Texas Instruments", "TI Programmer", GAME_SUPPORTS_SAVE | GAME_NO_SOUND_HW )
COMP( 1979, tibusan1, 0, 0, ti30,     tibusan1, driver_device, 0, "Texas Instruments", "TI Business Analyst-I", GAME_SUPPORTS_SAVE | GAME_NO_SOUND_HW )
