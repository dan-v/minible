/*!  \file     gui_prompts.c
*    \brief    Code dedicated to prompts and notifications
*    Created:  27/01/2019
*    Author:   Mathieu Stephan
*/


#ifndef GUI_PROMPTS_H_
#define GUI_PROMPTS_H_

#include "defines.h"

/* Defines */
// PIN prompt
#define PIN_PROMPT_MAX_TEXT_X   100
#define PIN_PROMPT_TEXT_Y       22
#define PIN_PROMPT_DIGIT_Y      20
#define PIN_PROMPT_ASTX_Y_INC   2
#define PIN_PROMPT_DIGIT_X_OFFS 140
#define PIN_PROMPT_DIGIT_X_SPC  17
#define PIN_PROMPT_DIGIT_Y_WDW  20
// Confirmation prompt
#define ONE_LINE_TEXT_FIRST_POS         5
#define TWO_LINE_TEXT_FIRST_POS         20
#define TWO_LINE_TEXT_SECOND_POS        40
#define THREE_LINE_TEXT_FIRST_POS       10
#define THREE_LINE_TEXT_SECOND_POS      26
#define THREE_LINE_TEXT_THIRD_POS       42
#define FOUR_LINE_TEXT_FIRST_POS        0
#define FOUR_LINE_TEXT_SECOND_POS       16
#define FOUR_LINE_TEXT_THIRD_POS        32
#define FOUR_LINE_TEXT_FOURTH_POS       48
#define CONF_PROMPT_MAX_TEXT_X          240
#define CONF_PROMPT_BITMAP_X            240
#define CONF_PROMPT_LINE_HEIGHT         14
// Delay when scrolling a text
#define SCROLLING_DEL                   33

/* Structs */
typedef struct
{
    cust_char_t* lines[4];
} confirmationText_t;

/* Prototypes */
void gui_prompts_render_pin_enter_screen(uint8_t* current_pin, uint16_t selected_digit, uint16_t stringID, int16_t anim_direction);
mini_input_yes_no_ret_te gui_prompts_ask_for_confirmation(uint16_t nb_args, confirmationText_t* text_object, BOOL flash_screen);
RET_TYPE gui_prompts_get_user_pin(volatile uint16_t* pin_code, uint16_t stringID);

#endif /* GUI_PROMPTS_H_ */