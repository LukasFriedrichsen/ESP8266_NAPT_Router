// esp_touch.h
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-05-05

#ifndef __ESP_TOUCH_H__
#define __ESP_TOUCH_H__

/*-------- structs and types ---------*/

typedef void (*esptouch_StartCallback)(void *arg);
typedef void (*esptouch_FailCallback)(void *arg);
typedef void (*esptouch_SuccessCallback)(void *arg);

struct esptouch_cb {
  esptouch_StartCallback esptouch_start_cb;
  esptouch_FailCallback esptouch_fail_cb;
  esptouch_SuccessCallback esptouch_suc_cb;
};

/*------------ functions -------------*/

bool esptouch_is_running(void);
bool esptouch_was_successful(void);
void esptouch_disable(void);
void esptouch_init(void);

#endif
