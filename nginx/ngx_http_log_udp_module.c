
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <linux/sockios.h>

#include <openssl/sha.h>

#define SHA_DIGEST_LENGTH 20
#define MD5_DIGEST_LENGTH 16

#define SECRET "SOMESECRET"

#define TSPORT 8789
#define TSIP "IP.IP.IP.IP"

struct _eventmsg {
  uint64_t tm;
  uint64_t bytessent;
  uint32_t ttype;
  uint32_t seconds;
  uint32_t tcpi_total_retrans;
  uint32_t tcpi_snd_mss;
  char ip[256];
  char sign[SHA_DIGEST_LENGTH];
} __attribute__ ((__packed__)) ;


typedef struct ngx_http_log_op_s  ngx_http_log_op_t;

typedef u_char *(*ngx_http_log_op_run_pt) (ngx_http_request_t *r, u_char *buf,
    ngx_http_log_op_t *op);

typedef size_t (*ngx_http_log_op_getlen_pt) (ngx_http_request_t *r,
    uintptr_t data);


struct ngx_http_log_op_s {
    size_t                      len;
    ngx_http_log_op_getlen_pt   getlen;
    ngx_http_log_op_run_pt      run;
    uintptr_t                   data;
};


typedef struct {
    ngx_str_t                   name;
    ngx_array_t                *flushes;
    ngx_array_t                *ops;        /* array of ngx_http_log_op_t */
} ngx_http_log_fmt_t;


typedef struct {
    ngx_array_t                 formats;    /* array of ngx_http_log_fmt_t */
    ngx_uint_t                  combined_used; /* unsigned  combined_used:1 */
} ngx_http_log_main_conf_t;


typedef struct {
    ngx_array_t                *lengths;
    ngx_array_t                *values;
} ngx_http_log_script_t;


typedef struct {
    ngx_open_file_t            *file;
    ngx_http_log_script_t      *script;
    time_t                      disk_full_time;
    time_t                      error_log_time;
    ngx_http_log_fmt_t         *format;
} ngx_http_log_t;


typedef struct {
    ngx_socket_t               udpsock;
} ngx_http_log_udp_loc_conf_t;


typedef struct {
    ngx_str_t                   name;
    size_t                      len;
    ngx_http_log_op_run_pt      run;
} ngx_http_log_var_t;



static void *ngx_http_log_udp_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_log_udp_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_log_udp_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_log_udp_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_log_udp_commands[] = { ngx_null_command };


static ngx_http_module_t  ngx_http_log_udp_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_log_udp_init,                     /* postconfiguration */

    ngx_http_log_udp_create_main_conf,         /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_log_udp_create_loc_conf,          /* create location configuration */
    ngx_http_log_udp_merge_loc_conf            /* merge location configuration */
};


ngx_module_t  ngx_http_log_udp_module = {
    NGX_MODULE_V1,
    &ngx_http_log_udp_module_ctx,              /* module context */
    ngx_http_log_udp_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


#define EVENT_DWLSTART 1
#define EVENT_DWLSTOP 2
#define EVENT_DWLFINISH 3

#ifdef	HAVE_SOCKADDR_DL_STRUCT
# include	<net/if_dl.h>
#endif

/* include sock_ntop */
char *
sock_ntop(const struct sockaddr *sa, socklen_t salen, char *str, size_t len)
{

	switch (sa->sa_family) {
	case AF_INET: {
		struct sockaddr_in	*sin = (struct sockaddr_in *) sa;

		if (inet_ntop(AF_INET, &sin->sin_addr, str, len) == NULL)
			return(NULL);
		return(str);
	}
/* end sock_ntop */

#ifdef	IPV6
	case AF_INET6: {
		struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *) sa;

		if (inet_ntop(AF_INET6, &sin6->sin6_addr, str, len) == NULL)
			return(NULL);
		return(str);
	}
#endif

#ifdef	AF_UNIX
	case AF_UNIX: {
		struct sockaddr_un	*unp = (struct sockaddr_un *) sa;

			/* OK to have no pathname bound to the socket: happens on
			   every connect() unless client calls bind() first. */
		if (unp->sun_path[0] == 0)
			strcpy(str, "(no pathname bound)");
		else
			snprintf(str, len, "%s", unp->sun_path);
		return(str);
	}
#endif

#ifdef	HAVE_SOCKADDR_DL_STRUCT
	case AF_LINK: {
		struct sockaddr_dl	*sdl = (struct sockaddr_dl *) sa;

		if (sdl->sdl_nlen > 0)
			snprintf(str, len, "%*s",
					 sdl->sdl_nlen, &sdl->sdl_data[0]);
		else
			snprintf(str, len, "AF_LINK, index=%d", sdl->sdl_index);
		return(str);
	}
#endif
	default:
		snprintf(str, len, "sock_ntop: unknown AF_xxx: %d, len %d",
				 sa->sa_family, salen);
		return(str);
	}
    return (NULL);
}


ngx_int_t
ngx_http_log_udp_handler(ngx_http_request_t *r)
{
    socklen_t                 tcplen;
    off_t                     bytesunsent;
    ngx_http_log_udp_loc_conf_t  *lcf;

    struct _eventmsg         msg;

    struct tcp_info tcp;

    unsigned char sign[SHA_DIGEST_LENGTH];

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http log_udp handler");

    if (ioctl(r->connection->fd, SIOCOUTQ, &bytesunsent))
        bytesunsent=0;

    if (r->connection->sent < bytesunsent)
        bytesunsent=0;

    if (! (r->connection->error || r->connection->unexpected_eof || r->connection->timedout || r->connection->destroyed) )
        bytesunsent=0;

    memset(&tcp, 0, sizeof(tcp));
    tcplen=sizeof(tcp);
    getsockopt(r->connection->fd, SOL_TCP, TCP_INFO, (void *)&tcp, &tcplen);

    memset(&msg, 0, sizeof(msg));

    msg.tm = ngx_time();
    msg.bytessent = r->connection->sent - bytesunsent;
    msg.tcpi_total_retrans = tcp.tcpi_total_retrans;
    msg.tcpi_snd_mss = tcp.tcpi_snd_mss; 
    msg.seconds = msg.tm - r->start_sec;


    sock_ntop(r->connection->sockaddr, r->connection->socklen, msg.ip, 255);

    strcpy(msg.sign, SECRET);
    SHA1((unsigned char *)&msg, sizeof(msg), sign);
    memcpy(msg.sign, sign, SHA_DIGEST_LENGTH);

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_log_udp_module);

    write(lcf->udpsock, &msg, sizeof(msg));


    return NGX_OK;
}



static void *
ngx_http_log_udp_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_log_main_conf_t  *conf;

    ngx_http_log_fmt_t  *fmt;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&conf->formats, cf->pool, 4, sizeof(ngx_http_log_fmt_t))
        != NGX_OK)
    {
        return NULL;
    }

    fmt = ngx_array_push(&conf->formats);
    if (fmt == NULL) {
        return NULL;
    }

    ngx_str_set(&fmt->name, "combined");

    fmt->flushes = NULL;

    fmt->ops = ngx_array_create(cf->pool, 16, sizeof(ngx_http_log_op_t));
    if (fmt->ops == NULL) {
        return NULL;
    }

    return conf;
}


static void *
ngx_http_log_udp_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_log_udp_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_log_udp_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->udpsock = 0;

    return conf;
}


static char *
ngx_http_log_udp_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_log_udp_loc_conf_t *conf = child;

    struct sockaddr_in serv;

    if (conf->udpsock == 0) {

        conf->udpsock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (conf->udpsock == -1)
            return NGX_CONF_ERROR;
        serv.sin_family = AF_INET;
        serv.sin_port = htons(TSPORT);
        serv.sin_addr.s_addr = inet_addr(TSIP);
        bzero(&(serv.sin_zero), 8);
        if (connect(conf->udpsock, (struct sockaddr *)&serv, sizeof(struct sockaddr_in))){
            close(conf->udpsock);
            conf->udpsock = 0;
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;

}



static ngx_int_t
ngx_http_log_udp_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;


    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_log_udp_handler;

    return NGX_OK;
}
