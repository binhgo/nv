/*
	Netvideo version 4.0
	Written by Ron Frederick <ronf@timeheart.net>

	IBM VCA frame grab headers

        IBM support added by Philip Papadopoulos <phil@msr.epm.ornl.gov> and
                             Al Geist <geist@msr.epm.ornl.gov>
*/

/*
 * Copyright (c) Xerox Corporation 1992. All rights reserved.
 *  
 * License is granted to copy, to use, and to make and to use derivative
 * works for research and evaluation purposes, provided that Xerox is
 * acknowledged in all documentation pertaining to any such copy or derivative
 * work. Xerox grants no other licenses expressed or implied. The Xerox trade
 * name should not be used in any advertising without its written permission.
 *  
 * XEROX CORPORATION MAKES NO REPRESENTATIONS CONCERNING EITHER THE
 * MERCHANTABILITY OF THIS SOFTWARE OR THE SUITABILITY OF THIS SOFTWARE
 * FOR ANY PARTICULAR PURPOSE.  The software is provided "as is" without
 * express or implied warranty of any kind.
 *  
 * These notices must be retained in any copies of any part of this software.
 */

extern int IBMVCA_Probe(void);
extern char *IBMVCA_Attach(void);
extern void IBMVCA_Detach(void);
extern grabproc_t *IBMVCA_Start(int min_framespacing, int config,
				reconfigproc_t *reconfig, void *enc_state);
extern void IBMVCA_Stop(void);
