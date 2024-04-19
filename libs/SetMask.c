#include <stdio.h>
#include <stdlib.h>
#include <fvwm/fvwmlib.h>
#include "../fvwm/module.h"

/***************************************************************************
 * 
 * Sets the which-message-types-do-I-want mask for modules 
 *
 **************************************************************************/
void SetMessageMask(int *fd, unsigned long mask)
{
  char set_mask_mesg[50];

  sprintf(set_mask_mesg,"SET_MASK %lu\n",mask);
  SendInfo(fd[0], set_mask_mesg, 0);
}







