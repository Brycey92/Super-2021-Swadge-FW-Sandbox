/*
 * QMA6981.h
 *
 *  Created on: Jul 9, 2019
 *      Author: adam
 */

#ifndef QMA6981_H_
#define QMA6981_H_

#include "user_config.h"

#if defined(FEATURE_ACCEL)
    bool QMA6981_setup(void);
    void QMA6981_poll(accel_t* currentAccel);
#endif

#endif /* QMA6981_H_ */
