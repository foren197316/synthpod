/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // getpid

#include <Ecore.h>
#include <Ecore_Con.h>
#include <Ecore_File.h>
#include <Efreet.h>

#include <osc.h>

#include <synthpod_app.h>
#include <synthpod_nsm.h>

struct _synthpod_nsm_t {
	char *url;
	char *call;
	char *exe;

	const synthpod_nsm_driver_t *driver;
	void *data;

	Ecore_Con_Server *serv;
	Ecore_Event_Handler *add;
	Ecore_Event_Handler *del;
	Ecore_Event_Handler *dat;

	osc_data_t send [0x10000];
	osc_data_t recv [0x10000];
};

static int
_reply(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	const char *target;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &target);

	//fprintf(stdout, "synthpod_nsm reply: %s\n", target);

	if(!strcmp(target, "/nsm/server/announce"))
	{
		const char *message;
		const char *manager;
		const char *capabilities;

		ptr = osc_get_string(ptr, &message);
		ptr = osc_get_string(ptr, &manager);
		ptr = osc_get_string(ptr, &capabilities);

		//TODO, e.g. toggle SM LED
	}

	return 1;
}

static int
_error(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	

	const char *msg;
	int32_t code;
	const char *err;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &msg);
	ptr = osc_get_int32(ptr, &code);
	ptr = osc_get_string(ptr, &err);

	fprintf(stderr, "synthpod_nsm error: #%i in %s: %s\n", code, msg, err);

	return 1;
}

static int
_client_open(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	
	
	const char *dir;
	const char *name;
	const char *id;

	const osc_data_t *ptr = buf;
	ptr = osc_get_string(ptr, &path);
	ptr = osc_get_string(ptr, &name);
	ptr = osc_get_string(ptr, &id);

	// open/create app
	int ret = nsm->driver->open(path, name, id, nsm->data);
	
	osc_data_t *buf0 = nsm->send;
	if(ret == 0)
	{
		ptr = osc_set_vararg(buf0, buf0+256, "/reply", "ss",
			"/nsm/client/open", "opened");
	}
	else
	{
		ptr = osc_set_vararg(buf0, buf0+256, "/error", "sis",
			"/nsm/client/open", 2, "opening failed");
	}

	size_t written = ptr ? ptr - buf0 : 0;
	if(written)
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		; //TODO

	return 1;
}

static int
_client_save(osc_time_t time, const char *path, const char *fmt, const osc_data_t *buf,
	size_t size, void *data)
{
	synthpod_nsm_t *nsm = data;	
	const osc_data_t *ptr;
	
	// save app
	int ret = nsm->driver->save(nsm->data);
	
	osc_data_t *buf0 = nsm->send;
	if(ret == 0)
	{
		ptr = osc_set_vararg(buf0, buf0+256, "/reply", "ss",
			"/nsm/client/save", "saved");
	}
	else
	{
		ptr = osc_set_vararg(buf0, buf0+256, "/error", "sis",
			"/nsm/client/save", 1, "save failed");
	}

	size_t written = ptr ? ptr - buf0 : 0;
	if(written)
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		; //TODO

	return 1;
}

static void
_announce(synthpod_nsm_t *nsm)
{
	// send announce message
	pid_t pid = getpid();

	osc_data_t *buf0 = nsm->send;
	osc_data_t *ptr = osc_set_vararg(buf0, buf0+512, "/nsm/server/announce", "sssiii",
		nsm->call, ":message:", nsm->exe, 1, 2, pid);

	size_t written = ptr ? ptr - buf0 : 0;
	if(written)
		ecore_con_server_send(nsm->serv, nsm->send, written);
	else
		; //TODO
}

static const osc_method_t methods [] = {
	{"/reply", NULL, _reply},
	{"/error", "sis", _error},
	
	{"/nsm/client/open", "sss", _client_open},
	{"/nsm/client/save", "", _client_save},

	{NULL, NULL, NULL}
};

static Eina_Bool
_con_add(void *data, int type, void *info)
{
	synthpod_nsm_t *nsm = data;
	Ecore_Con_Event_Client_Add *ev = info;

	assert(type == ECORE_CON_EVENT_SERVER_ADD);

	//printf("_client_add\n");
	//TODO
			
	_announce(nsm);

	return EINA_TRUE;
}

static Eina_Bool
_con_del(void *data, int type, void *info)
{
	synthpod_nsm_t *nsm = data;
	Ecore_Con_Event_Client_Del *ev = info;

	assert(type == ECORE_CON_EVENT_SERVER_DEL);
	
	//printf("_client_del\n");
	//TODO

	return EINA_TRUE;
}

static Eina_Bool
_con_dat(void *data, int type, void *info)
{
	synthpod_nsm_t *nsm = data;
	Ecore_Con_Event_Client_Data *ev = info;

	assert(type == ECORE_CON_EVENT_SERVER_DATA);
	
	//printf("_client_data\n");

	if(osc_check_packet(ev->data, ev->size))
		osc_dispatch_method(0, ev->data, ev->size, methods, NULL, NULL, nsm);
	else
		fprintf(stderr, "_client_dat: malformed OSC packet\n");

	return EINA_TRUE;
}

synthpod_nsm_t *
synthpod_nsm_new(const char *exe, const char *path,
	const synthpod_nsm_driver_t *nsm_driver, void *data)
{
	efreet_init();

	if(!nsm_driver)
		return NULL;

	synthpod_nsm_t *nsm = calloc(1, sizeof(synthpod_nsm_t));
	if(!nsm)
		return NULL;

	nsm->driver = nsm_driver;
	nsm->data = data;

	nsm->call = strdup("Synthpod");
	nsm->exe = exe ? strdup(exe) : NULL;
	
	nsm->url = getenv("NSM_URL");
	if(nsm->url)
	{
		nsm->url = strdup(nsm->url);
		if(!nsm->url)
			return NULL;
		
		//printf("url: %s\n", nsm->url);

		Ecore_Con_Type type;
		if(!strncmp(nsm->url, "osc.udp", 7))
			type = ECORE_CON_REMOTE_UDP;
		else if(!strncmp(nsm->url, "osc.tcp", 7))
			type = ECORE_CON_REMOTE_TCP;
		else
			goto fail;

		char *addr = strstr(nsm->url, "://");
		if(!addr)
			goto fail;
		addr += 3; // skip "://"

		char *dst = strchr(addr, ':');
		if(!dst)
			goto fail;
		*dst++ = '\0';

		uint16_t port;
		if(sscanf(dst, "%hu", &port) != 1)
			goto fail;
		
		printf("addr: %s, dst: %hu\n", addr, port);

		nsm->serv = ecore_con_server_connect(type,
			addr, port, nsm);
		if(!nsm->serv)
			goto fail;

		nsm->add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, _con_add, nsm);
		nsm->del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, _con_del, nsm);
		nsm->dat = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, _con_dat, nsm);

		if(type == ECORE_CON_REMOTE_UDP)
			_announce(nsm);
	}
	else
	{
		const char *data_dir = efreet_data_home_get();

		if(path)
		{
			nsm->driver->open(path,
				nsm->call, nsm->exe, nsm->data);
		}
		else
		{
			char *synthpod_dir = NULL;
			asprintf(&synthpod_dir, "%s/synthpod", data_dir);
			if(synthpod_dir)
			{
				ecore_file_mkpath(synthpod_dir);

				nsm->driver->open(synthpod_dir,
					nsm->call, nsm->exe, nsm->data);

				free(synthpod_dir);
			}
		}
	}

	return nsm;

fail:
	if(nsm->url)
		free(nsm->url);

	return NULL;
}

void
synthpod_nsm_free(synthpod_nsm_t *nsm)
{
	if(nsm)
	{
		if(nsm->call)
			free(nsm->call);
		if(nsm->exe)
			free(nsm->exe);

		if(nsm->url)
		{
			if(nsm->add)
				ecore_event_handler_del(nsm->add);
			if(nsm->del)
				ecore_event_handler_del(nsm->del);
			if(nsm->dat)
				ecore_event_handler_del(nsm->dat);
			if(nsm->serv)
				ecore_con_server_del(nsm->serv);

			free(nsm->url);
		}
		else
		{
			// directly call save callback
			nsm->driver->save(nsm->data);
		}

		free(nsm);
	}

	efreet_shutdown();
}
