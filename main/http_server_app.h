#ifndef __HTTP_SERVER_APP_H
#define __HTTP_SERVER_APP_H


typedef void (* http_post_callback_t) (char* data, int length);
typedef void (* http_get_callback_t) (void);
typedef void (* http_get_data_callback_t) (char* data, int length);

void start_webserver(void);
void stop_webserver(void);

void http_set_callback_switch(void *cb);
void http_set_callback_fanspeed(void *cb);
void http_set_callback_voice(void *cb);
void http_set_callback_dht11(void *cb);
void http_set_callback_rgb(void *cb);
void http_set_callback_time(void *cb);
void http_set_callback_capture(void *cb);
void dht11_response( char *data, int len);


#endif



