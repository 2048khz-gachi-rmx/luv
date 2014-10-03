/*
 *  Copyright 2014 The Luvit Authors. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
#include "luv.h"

static int luv_disable_stdio_inheritance(lua_State* L) {
  uv_disable_stdio_inheritance();
  return 0;
}

static uv_process_t* luv_check_process(lua_State* L, int index) {
  uv_process_t* handle = luaL_checkudata(L, index, "uv_handle");
  luaL_argcheck(L, handle->type = UV_PROCESS, index, "Expected uv_process_t");
  return handle;
}

static void exit_cb(uv_process_t* handle, int64_t exit_status, int term_signal) {
  luv_handle_t* data = handle->data;
  luv_find_handle(R, data);
  lua_pushinteger(R, exit_status);
  lua_pushinteger(R, term_signal);
  luv_call_callback(R, data, LUV_EXIT, 3);
}

static int luv_spawn(lua_State* L) {
  uv_process_t* handle;
  uv_process_options_t options;
  size_t i, len = 0;
  int ret;

  memset(&options, 0, sizeof(options));
  options.exit_cb = exit_cb;
  options.file = luaL_checkstring(L, 1);
  options.flags = 0;

  // Make sure the 2nd argument is a table
  luaL_checktype(L, 2, LUA_TTABLE);

  // get the args list
  lua_getfield(L, 2, "args");
  // +1 for inserted command at front
  if (lua_type(L, -1) == LUA_TTABLE) {
    len = 1 + lua_rawlen(L, -1);
  }
  else if (lua_type(L, -1) != LUA_TNIL) {
    luaL_argerror(L, 3, "args option must be table");
  }
  else {
    len = 1;
  }
  // +1 for null terminator at end
  options.args = malloc((len + 1) * sizeof(*options.args));
  options.args[0] = (char*)options.file;
  for (i = 1; i < len; ++i) {
    lua_rawgeti(L, -1, i);
    options.args[i] = (char*)lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  options.args[len] = NULL;
  lua_pop(L, 1);

  // get the stdio list
  lua_getfield(L, 2, "stdio");
  if (lua_type(L, -1) == LUA_TTABLE) {
    options.stdio_count = len = lua_rawlen(L, -1);
    options.stdio = malloc(len * sizeof(*options.stdio));
    for (i = 0; i < len; ++i) {
      lua_rawgeti(L, -1, i + 1);
      // integers are assumed to be file descripters
      if (lua_type(L, -1) == LUA_TNUMBER) {
        options.stdio[i].flags = UV_INHERIT_FD;
        options.stdio[i].data.fd = lua_tointeger(L, -1);
      }
      // userdata is assumed to be a uv_stream_t instance
      else if (lua_type(L, -1) == LUA_TUSERDATA) {
        int fd;
        uv_stream_t* stream = luv_check_stream(L, -1);
        int err = uv_fileno((uv_handle_t*)stream, &fd);
        if (err == UV_EINVAL || err == UV_EBADF) {
          options.stdio[i].flags = UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE;
        }
        else {
          options.stdio[i].flags = UV_INHERIT_STREAM;
        }
        options.stdio[i].data.stream = stream;
      }
      else if (lua_type(L, -1) == LUA_TNIL) {
        options.stdio[i].flags = UV_IGNORE;
      }
      else {
        luaL_argerror(L, 2, "stdio table entries must be nil, uv_stream_t, or integer");
      }
      lua_pop(L, 1);
    }
  }
  else if (lua_type(L, -1) != LUA_TNIL) {
    luaL_argerror(L, 2, "stdio option must be table");
  }
  lua_pop(L, 1);

  // Get the env
  lua_getfield(L, 2, "env");
  if (lua_type(L, -1) == LUA_TTABLE) {
    len = lua_rawlen(L, -1);
    options.env = malloc((len + 1) * sizeof(*options.env));
    for (i = 0; i < len; ++i) {
      lua_rawgeti(L, -1, i + 1);
      options.env[i] = (char*)lua_tostring(L, -1);
      lua_pop(L, 1);
    }
    options.env[len] = NULL;
  }
  else if (lua_type(L, -1) != LUA_TNIL) {
    luaL_argerror(L, 2, "env option must be table");
  }
  lua_pop(L, 1);

  // Get the cwd
  lua_getfield(L, 2, "cwd");
  if (lua_type(L, 01) == LUA_TSTRING) {
    options.cwd = (char*)lua_tostring(L, -1);
  }
  else if (lua_type(L, -1) != LUA_TNIL) {
    luaL_argerror(L, 2, "cwd option must be string");
  }
  lua_pop(L, 1);

  // Check for uid
  lua_getfield(L, 2, "uid");
  if (lua_type(L, -1) == LUA_TNUMBER) {
    options.uid = lua_tointeger(L, -1);
    options.flags |= UV_PROCESS_SETUID;
  }
  else if (lua_type(L, -1) != LUA_TNIL) {
    luaL_argerror(L, 2, "uid option must be number");
  }
  lua_pop(L, 1);

  // Check for gid
  lua_getfield(L, 2, "gid");
  if (lua_type(L, -1) == LUA_TNUMBER) {
    options.gid = lua_tointeger(L, -1);
    options.flags |= UV_PROCESS_SETGID;
  }
  else if (lua_type(L, -1) != LUA_TNIL) {
    luaL_argerror(L, 2, "gid option must be number");
  }
  lua_pop(L, 1);

  // Check for the boolean flags
  lua_getfield(L, 2, "verbatim");
  if (lua_toboolean(L, -1)) {
    options.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
  }
  lua_pop(L, 1);
  lua_getfield(L, 2, "detached");
  if (lua_toboolean(L, -1)) {
    options.flags |= UV_PROCESS_DETACHED;
  }
  lua_pop(L, 1);
  lua_getfield(L, 2, "hide");
  if (lua_toboolean(L, -1)) {
    options.flags |= UV_PROCESS_WINDOWS_HIDE;
  }
  lua_pop(L, 1);

  handle = lua_newuserdata(L, sizeof(*handle));
  handle->data = luv_setup_handle(L);

  if (!lua_isnoneornil(L, 3)) {
    luv_check_callback(L, handle->data, LUV_EXIT, 3);
  }

  ret = uv_spawn(uv_default_loop(), handle, &options);

  free(options.args);
  free(options.stdio);
  free(options.env);
  if (ret < 0) return luv_error(L, ret);
  lua_pushinteger(L, handle->pid);
  return 2;
}

static int luv_parse_signal(lua_State* L, int slot) {
  if (lua_isnumber(L, slot)) {
    return lua_tonumber(L, slot);
  }
  if (lua_isstring(L, slot)) {
const char* string = lua_tostring(L, slot);
#ifdef SIGHUP
    if (strcasecmp(string, "sighup") == 0) return SIGHUP;
#endif
#ifdef SIGINT
    if (strcasecmp(string, "sigint") == 0) return SIGINT;
#endif
#ifdef SIGQUIT
    if (strcasecmp(string, "sigquit") == 0) return SIGQUIT;
#endif
#ifdef SIGILL
    if (strcasecmp(string, "sigill") == 0) return SIGILL;
#endif
#ifdef SIGTRAP
    if (strcasecmp(string, "sigtrap") == 0) return SIGTRAP;
#endif
#ifdef SIGABRT
    if (strcasecmp(string, "sigabrt") == 0) return SIGABRT;
#endif
#ifdef SIGIOT
    if (strcasecmp(string, "sigiot") == 0) return SIGIOT;
#endif
#ifdef SIGBUS
    if (strcasecmp(string, "sigbus") == 0) return SIGBUS;
#endif
#ifdef SIGFPE
    if (strcasecmp(string, "sigfpe") == 0) return SIGFPE;
#endif
#ifdef SIGKILL
    if (strcasecmp(string, "sigkill") == 0) return SIGKILL;
#endif
#ifdef SIGUSR1
    if (strcasecmp(string, "sigusr1") == 0) return SIGUSR1;
#endif
#ifdef SIGSEGV
    if (strcasecmp(string, "sigsegv") == 0) return SIGSEGV;
#endif
#ifdef SIGUSR2
    if (strcasecmp(string, "sigusr2") == 0) return SIGUSR2;
#endif
#ifdef SIGPIPE
    if (strcasecmp(string, "sigpipe") == 0) return SIGPIPE;
#endif
#ifdef SIGALRM
    if (strcasecmp(string, "sigalrm") == 0) return SIGALRM;
#endif
#ifdef SIGTERM
    if (strcasecmp(string, "sigterm") == 0) return SIGTERM;
#endif
#ifdef SIGCHLD
    if (strcasecmp(string, "sigchld") == 0) return SIGCHLD;
#endif
#ifdef SIGSTKFLT
    if (strcasecmp(string, "sigstkflt") == 0) return SIGSTKFLT;
#endif
#ifdef SIGCONT
    if (strcasecmp(string, "sigcont") == 0) return SIGCONT;
#endif
#ifdef SIGSTOP
    if (strcasecmp(string, "sigstop") == 0) return SIGSTOP;
#endif
#ifdef SIGTSTP
    if (strcasecmp(string, "sigtstp") == 0) return SIGTSTP;
#endif
#ifdef SIGBREAK
    if (strcasecmp(string, "sigbreak") == 0) return SIGBREAK;
#endif
#ifdef SIGTTIN
    if (strcasecmp(string, "sigttin") == 0) return SIGTTIN;
#endif
#ifdef SIGTTOU
    if (strcasecmp(string, "sigttou") == 0) return SIGTTOU;
#endif
#ifdef SIGURG
    if (strcasecmp(string, "sigurg") == 0) return SIGURG;
#endif
#ifdef SIGXCPU
    if (strcasecmp(string, "sigxcpu") == 0) return SIGXCPU;
#endif
#ifdef SIGXFSZ
    if (strcasecmp(string, "sigxfsz") == 0) return SIGXFSZ;
#endif
#ifdef SIGVTALRM
    if (strcasecmp(string, "sigvtalrm") == 0) return SIGVTALRM;
#endif
#ifdef SIGPROF
    if (strcasecmp(string, "sigprof") == 0) return SIGPROF;
#endif
#ifdef SIGWINCH
    if (strcasecmp(string, "sigwinch") == 0) return SIGWINCH;
#endif
#ifdef SIGIO
    if (strcasecmp(string, "sigio") == 0) return SIGIO;
#endif
#ifdef SIGPOLL
# if SIGPOLL != SIGIO
    if (strcasecmp(string, "sigpoll") == 0) return SIGPOLL;
# endif
#endif
#ifdef SIGLOST
    if (strcasecmp(string, "siglost") == 0) return SIGLOST;
#endif
#ifdef SIGPWR
# if SIGPWR != SIGLOST
    if (strcasecmp(string, "sigpwr") == 0) return SIGPWR;
# endif
#endif
#ifdef SIGSYS
    if (strcasecmp(string, "sigsys") == 0) return SIGSYS;
#endif
    return luaL_error(L, "Unknown signal '%s'", string);
  }
  return SIGTERM;
}

static int luv_process_kill(lua_State* L) {
  uv_process_t* handle = luv_check_process(L, 1);
  int signum = luv_parse_signal(L, 2);
  int ret = uv_process_kill(handle, signum);
  if (ret < 0) return luv_error(L, ret);
  lua_pushinteger(L, ret);
  return 1;
}

static int luv_kill(lua_State* L) {
  int pid = luaL_checkinteger(L, 1);
  int signum = luv_parse_signal(L, 2);
  int ret = uv_kill(pid, signum);
  if (ret < 0) return luv_error(L, ret);
  lua_pushinteger(L, ret);
  return 1;
}
