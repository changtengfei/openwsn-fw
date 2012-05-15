/**
\brief GINA-specific definition of the "eui64" bsp module.

\author Thomas Watteyne <watteyne@eecs.berkeley.edu>, March 2012.
*/

#include "string.h"
#include "eui64.h"

//=========================== defines =========================================

//=========================== variables =======================================

// address is flash where GINA's EUI64 identifier is stored
__no_init volatile uint8_t eui64 @ 0x10ee;

//=========================== prototypes ======================================

//=========================== public ==========================================

void eui64_get(uint8_t* addressToWrite) {
   memcpy(addressToWrite,(void const*)&eui64,8);
}

//=========================== private =========================================