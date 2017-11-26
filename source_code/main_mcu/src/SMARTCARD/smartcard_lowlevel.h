 /* CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/license_cddl-1.0.txt
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/license_cddl-1.0.txt
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */


#ifndef SMARTCARD_H_
#define SMARTCARD_H_

#include "platform_defines.h"
#include "defines.h"
#include <stdint.h>

// DEFINES
#define MAN_FUSE    0x00
#define EC2EN_FUSE  0x01
#define ISSUER_FUSE 0x02

// Prototypes
uint8_t* smartcard_lowlevel_read_smc(uint16_t nb_bytes_total_read, uint16_t start_record_index, uint8_t* data_to_receive);
void smartcard_lowlevel_write_smc(uint16_t start_index_bit, uint16_t nb_bits, uint8_t* data_to_write);
card_detect_return_te smartcard_lowlevel_first_detect_function(void);
det_ret_type_te smartcard_lowlevel_is_card_plugged(void);
void smartcard_lowlevel_write_nerase(uint8_t is_write);
void smartcard_lowlevel_inverted_clock_pulse(void);
void smartcard_lowlevel_clear_pgmrst_signals(void);
void smartcard_lowlevel_set_pgmrst_signals(void);
void smartcard_lowlevel_hpulse_delay(void);
void smartcard_lowlevel_clock_pulse(void);
void smartcard_lowlevel_detect(void);

// old prototypes
void writeSMC(uint16_t start_index_bit, uint16_t nb_bits, uint8_t* data_to_write);
void eraseApplicationZone1NZone2SMC(uint8_t zone1_nzone2);
RET_TYPE securityValidationSMC(volatile uint16_t* code);
RET_TYPE firstDetectFunctionSMC(void);
void blowFuse(uint8_t fuse_name);
void smartcardHPulseDelay(void);
RET_TYPE isCardPlugged(void);
void removeFunctionSMC(void);
void setSPIModeSMC(void);
void initPortSMC(void);

// Macros
/*! \fn     isSmartCardAbsent(void)
*   \brief  Function used to check if the smartcard is absent
*   \note   This function should only be used to check if the smartcard is absent. It works because scanSMCDectect reports the
*           smartcard absent when it is not here during only one tick. It also works because the smartcard is always reported
*           released via isCardPlugged
*   \return RETURN_OK if absent
*/
static inline RET_TYPE isSmartCardAbsent(void)
{
    if (!((PORT->Group[SMC_DET_GROUP].IN.reg & SMC_DET_MASK) == 0))
    {
        return RETURN_NOK;
    }
    else
    {
        return RETURN_OK;
    }
}

// Defines
#define SMARTCARD_FABRICATION_ZONE	0x0F0F
#define SMARTCARD_FACTORY_PIN		0xF0F0
#define SMARTCARD_DEFAULT_PIN		0xF0F0
#define SMARTCARD_AZ_BIT_LENGTH     512
#define SMARTCARD_AZ1_BIT_START     176
#define SMARTCARD_AZ1_BIT_RESERVED  16
#define SMARTCARD_MTP_PASS_LENGTH   (SMARTCARD_AZ_BIT_LENGTH - SMARTCARD_AZ1_BIT_RESERVED - AES_KEY_LENGTH)
#define SMARTCARD_MTP_PASS_OFFSET   (SMARTCARD_AZ1_BIT_RESERVED + AES_KEY_LENGTH)
#define SMARTCARD_AZ2_BIT_START     736
#define SMARTCARD_AZ2_BIT_RESERVED  16
#define SMARTCARD_MTP_LOGIN_LENGTH  (SMARTCARD_AZ_BIT_LENGTH - SMARTCARD_AZ2_BIT_RESERVED)
#define SMARTCARD_MTP_LOGIN_OFFSET  SMARTCARD_AZ2_BIT_RESERVED
#define SMARTCARD_CPZ_LENGTH        8

#endif /* SMARTCARD_H_ */