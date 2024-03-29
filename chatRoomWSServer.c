/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "mongoose.h"

static sig_atomic_t s_signal_received = 0;
static const char *s_http_port = "8000";
static struct mg_serve_http_opts s_http_server_opts;

static void signal_handler(int sig_num) {
  signal(sig_num, signal_handler);  // Reinstantiate signal handler
  s_signal_received = sig_num;
}

static int is_websocket(const struct mg_connection *nc) {
  return nc->flags & MG_F_IS_WEBSOCKET;
}

static void broadcast(struct mg_connection *nc, const struct mg_str msg) {
  struct mg_connection *c;
  printf("(Broadcast) \t[%d]\n", (int)msg.len);
  for (c = mg_next(nc->mgr, NULL); c != NULL; c = mg_next(nc->mgr, c)) {
    if (c == nc) continue; /* Don't send to the sender. */
    mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg.p, (int)msg.len);
  }
}

void unicast(struct mg_connection *nc, const struct mg_str msg) {
  struct mg_connection *c;
  char recipient_floID[35];
  snprintf(recipient_floID, sizeof(recipient_floID), "%.*s", 34, &msg.p[1]);
  printf("%s \t[%d]\n",recipient_floID, (int)msg.len - 36); /* Local echo. */
  for (c = mg_next(nc->mgr, NULL); c != NULL; c = mg_next(nc->mgr, c)) {
    if (!strcmp(c->floID,recipient_floID))  /* Send to receiver */
      mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT, &msg.p[36], (int)msg.len - 36);
  }
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
  switch (ev) {
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
      /* New websocket connection. Tell everybody. */
      break;
    }
    case MG_EV_WEBSOCKET_FRAME: {
      struct websocket_message *wm = (struct websocket_message *) ev_data;
      /* New websocket message. Tell everybody. */
      struct mg_str d = {(char *) wm->data, wm->size};
      if (d.p[0] == '$')
        snprintf(nc->floID, sizeof(nc->floID), "%.*s", 34, &d.p[1]);
      else if ((d.p[0] == '>'))
        unicast(nc, d);
      else
        broadcast(nc, d);
      break;
    }
    case MG_EV_HTTP_REQUEST: {
      mg_serve_http(nc, (struct http_message *) ev_data, s_http_server_opts);
      break;
    }
    case MG_EV_CLOSE: {
      /* Disconnect. Tell everybody. */
      if (is_websocket(nc)) {
        //broadcast(nc, mg_mk_str("-- left"));
      }
      break;
    }
  }
}

int main(void) {
  struct mg_mgr mgr;
  struct mg_connection *nc;

  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IOLBF, 0);

  mg_mgr_init(&mgr, NULL);

  nc = mg_bind(&mgr, s_http_port, ev_handler);
  mg_set_protocol_http_websocket(nc);
  s_http_server_opts.document_root = "web";  // Serve current directory
  s_http_server_opts.enable_directory_listing = "no";

  printf("Started on port %s\n", s_http_port);
  while (s_signal_received == 0) {
    mg_mgr_poll(&mgr, 200);
  }
  mg_mgr_free(&mgr);

  return 0;
}
