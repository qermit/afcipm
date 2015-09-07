/*
 * board_defs.h
 *
 *   AFCIPMI  --
 *
 *   Copyright (C) 2015  Henrique Silva <henrique.silva@lnls.br>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BOARD_DEFS_H_
#define BOARD_DEFS_H_

//#ifndef BOARD
#define BOARD_MBED   0
#define BOARD_AFC    1
#define BOARD_AFCK   2

#ifdef BOARD
#undef BOARD
#endif

#define BOARD_VERSION_MBED      1

#define BOARD_VERSION_AFC_V1    1
#define BOARD_VERSION_AFC_V2    2
#define BOARD_VERSION_AFC_V3    3
#define BOARD_VERSION_AFC_V3_1  4

#define BOARD           BOARD_MBED
#define BOARD_VERSION   BOARD_VERSION_AFC_V3


//#endif

#if (BOARD == BOARD_AFC)
#if (BOARD_VERSION == BOARD_VERSION_AFC_V3)

/* I2C Pins definitions */

#define I2C0_PORT       0
#define I2C0_SDA_PIN    27
#define I2C0_SCL_PIN    28
#define I2C0_PIN_FUNC   1

#define I2C1_PORT       0
#define I2C1_SDA_PIN    0
#define I2C1_SCL_PIN    1
#define I2C1_PIN_FUNC   3

#define I2C2_PORT       0
#define I2C2_SDA_PIN    10
#define I2C2_SCL_PIN    11
#define I2C2_PIN_FUNC   2

/* Geographic Address pin definitions */
#define GA0_PORT         1
#define GA1_PORT         1
#define GA2_PORT         1
#define GA_TEST_PORT    1
#define GA0_PIN         0
#define GA1_PIN         1
#define GA2_PIN         4
#define GA_TEST_PIN     8

#define ledBLUE_PORT    1
#define ledBLUE_PIN     9
#define ledGREEN_PORT   1
#define ledGREEN_PIN    10
#define ledRED_PORT     1
#define ledRED_PIN      25

#endif
#elif (BOARD == BOARD_MBED)

/* I2C Pins definitions */

#define I2C0_PORT       0
#define I2C0_SDA_PIN    27
#define I2C0_SCL_PIN    28
#define I2C0_PIN_FUNC   1

#define I2C1_PORT       0
#define I2C1_SDA_PIN    0
#define I2C1_SCL_PIN    1
#define I2C1_PIN_FUNC   3

#define I2C2_PORT       0
#define I2C2_SDA_PIN    10
#define I2C2_SCL_PIN    11
#define I2C2_PIN_FUNC   2

/* Geographic Address pin definitions */
#define GA_PORT         1
#define GA0_PIN         0
#define GA1_PIN         1
#define GA2_PIN         4
#define GA_TEST_PIN     8

#define ledBLUE_PORT    1
#define ledBLUE_PIN     18
#define ledGREEN_PORT   1
#define ledGREEN_PIN    20
#define ledRED_PORT     1
#define ledRED_PIN      21

#else
#error "Unknown board"
#endif

#endif
