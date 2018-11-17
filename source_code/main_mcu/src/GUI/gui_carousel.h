/*!  \file     gui_carousel.h
*    \brief    GUI carousel rendering functions
*    Created:  17/11/2018
*    Author:   Mathieu Stephan
*/
#include <stdio.h>

#ifndef GUI_CAROUSEL_H_
#define GUI_CAROUSEL_H_

/* Defines */
// Big, medium, small icon X/Y
#define CAROUSEL_BIG_EDGE           48
#define CAROUSEL_MID_EDGE           36
#define CAROUSEL_SMALL_EDGE         24
// Edge increment for animation
#define CAROUSEL_Y_ANIM_STEP        2
// Number of bitmap for given icon
#define CAROUSEL_NB_SCALED_ICONS    (((CAROUSEL_BIG_EDGE-CAROUSEL_SMALL_EDGE)/CAROUSEL_Y_ANIM_STEP)+1)
// Y on each all icons are aligned
#define CAROUSEL_Y_ALIGN            24

/* Prototypes */
void gui_carousel_render_animation(uint16_t nb_elements, const uint16_t* pic_ids, const uint16_t* text_ids, uint16_t selected_id, BOOL left_anim);
void gui_carousel_render(uint16_t nb_elements, const uint16_t* pic_ids, const uint16_t* text_ids, uint16_t selected_id, int16_t anim_step);


#endif /* GUI_CAROUSEL_H_ */