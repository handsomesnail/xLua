/*---------------------------------------------------------------------------------------------
 *  Copyright (c) 2021 - present handsomesnail
 *  Licensed under the MIT License. See LICENSE in the project root for more information.
 *--------------------------------------------------------------------------------------------*/

#define LUA_LIB

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <stdint.h>
#include <string.h>
#include "i64lib.h"

#if USING_LUAJIT
#include "lj_obj.h"
#else
#include "lstate.h"
#endif

#include "sidlapi.h"

using namespace SidlRT;

static int sidlrt_has_field(lua_State *L);
static int sidlrt_get_field(lua_State *L);
static int sidlrt_set_field_value(lua_State *L);
static int sidlrt_set_field_obj(lua_State *L);

/// @brief SidlObject是否存在字段 local hasField = sidlrt.hasfield(obj.InstanceId, "FieldName")
static int sidlrt_has_field(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Instance id doesn't match uint64 type");
  }
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Field name doesn't match string type");
  }
  uint64_t instance_id = lua_touint64(L, 1);
  const char *field_name = lua_tostring(L, 2);
  lua_pushboolean(L, SidlAPI_HasField(instance_id, field_name));
  return 1;
}

/// @brief 获取SidlObject的字段
/// local fieldValue,isObj = sidlrt.getfield(obj.InstanceId, "FieldName")
static int sidlrt_get_field(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Instance id doesn't match uint64 type");
  }
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Field name doesn't match string type");
  }
  uint64_t instance_id = lua_touint64(L, 1);
  const char *field_name = lua_tostring(L, 2);
  SidlFieldType sidl_field_type = SidlAPI_GetFieldType(instance_id, field_name);
  if (sidl_field_type == SidlFieldType::UNKNOWN) {
    return luaL_error(L, "The field \"%s\" doesn't exist", field_name);
  }
  SidlValue value = SidlAPI_GetFieldValue(instance_id, field_name);
  if (sidl_field_type == SidlFieldType::INT || sidl_field_type == SidlFieldType::ENUM) {
    lua_pushinteger(L, value.intValue);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::LONG) {
    lua_pushint64(L, value.longValue);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::FLOAT) {
    lua_pushnumber(L, value.floatValue);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::DOUBLE) {
    lua_pushnumber(L, value.doubleValue);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::BOOL) {
    lua_pushboolean(L, value.booleanValue);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::STRING) {
    lua_pushstring(L, value.stringValue);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::OBJECT || sidl_field_type == SidlFieldType::ARRAY ||
             sidl_field_type == SidlFieldType::MAP) {
    if (value.objectInstanceId == 0) {
      lua_pushnil(L);
      lua_pushboolean(L, 0);
    } else {
      lua_pushuint64(L, value.objectInstanceId);
      lua_pushboolean(L, 1);  // isObj返回1
    }
  }
  return 2;
}

/// @brief 设置SidlObject的字段(注意显式传入nil仍会被作为参数压栈)
/// local isObj = sidlrt.setfieldvalue(obj.InstanceId, "FieldName", fieldValue)
static int sidlrt_set_field_value(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Instance id doesn't match uint64 type");
  }
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Field name doesn't match string type");
  }
  uint64_t instance_id = lua_touint64(L, 1);
  const char *field_name = lua_tostring(L, 2);
  SidlFieldType sidl_field_type = SidlAPI_GetFieldType(instance_id, field_name);
  if (sidl_field_type == SidlFieldType::UNKNOWN) {
    return luaL_error(L, "The field \"%s\" doesn't exist", field_name);
  }
  int value_idx = 3;
  SidlValue value = SidlValue();
  if (sidl_field_type == SidlFieldType::INT || sidl_field_type == SidlFieldType::ENUM) {
    if (!lua_isnumber(L, value_idx)) {
      return luaL_error(L, "The field \"%s\" value doesn't match int type", field_name);
    }
    value.intValue = lua_tointeger(L, value_idx);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::LONG) {
    if (!lua_isnumber(L, value_idx)) {
      return luaL_error(L, "The field \"%s\" value doesn't match long type", field_name);
    }
    value.longValue = lua_toint64(L, value_idx);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::FLOAT) {
    if (!lua_isnumber(L, value_idx)) {
      return luaL_error(L, "The field \"%s\" value doesn't match float type", field_name);
    }
    value.floatValue = lua_tonumber(L, value_idx);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::DOUBLE) {
    if (!lua_isnumber(L, value_idx)) {
      return luaL_error(L, "The field \"%s\" value doesn't match double type", field_name);
    }
    value.doubleValue = lua_tonumber(L, value_idx);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::BOOL) {
    if (!lua_isboolean(L, value_idx)) {
      return luaL_error(L, "The field \"%s\" value doesn't match boolean type", field_name);
    }
    value.booleanValue = lua_toboolean(L, value_idx);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::STRING) {
    if (!lua_isstring(L, value_idx)) {
      return luaL_error(L, "The field \"%s\" value doesn't match string type", field_name);
    }
    value.stringValue = lua_tostring(L, value_idx);
    lua_pushboolean(L, 0);
  } else if (sidl_field_type == SidlFieldType::OBJECT || sidl_field_type == SidlFieldType::ARRAY ||
             sidl_field_type == SidlFieldType::MAP) {
    if (lua_isnil(L, value_idx)) {
      // 允许直接给obj赋nil值
      value.objectInstanceId = 0;
      lua_pushboolean(L, 0);
    } else {
      lua_pushboolean(L, 1);  // isObj返回1
    }
  }
  SidlAPI_SetFieldValue(instance_id, field_name, value);
  return 1;
}

/// @brief 设置SidlObject的字段(调用前已判断setfieldvalue返回true不用重复判断SidlFieldType)
/// sidlrt.setfieldobj(obj.InstanceId, "FieldName", fieldObj.InstanceId)
static int sidlrt_set_field_obj(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Instance id doesn't match uint64 type");
  }
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Field name doesn't match string type");
  }
  uint64_t instance_id = lua_touint64(L, 1);
  const char *field_name = lua_tostring(L, 2);
  int value_idx = 3;
  SidlValue value = SidlValue();
  if (lua_isnil(L, value_idx)) {
    value.objectInstanceId = 0;
  } else if (lua_isnumber(L, value_idx)) {
    value.objectInstanceId = lua_touint64(L, value_idx);
  } else {
    return luaL_error(L, "The field \"%s\" value doesn't match object type", field_name);
  }
  SidlAPI_SetFieldValue(instance_id, field_name, value);
  return 0;
}

static const luaL_Reg sidlrtlib[] = {{"hasfield", sidlrt_has_field},
                                     {"getfield", sidlrt_get_field},
                                     {"setfieldvalue", sidlrt_set_field_value},
                                     {"setfieldobj", sidlrt_set_field_obj},
                                     {NULL, NULL}};

EXPORT void CALL luaopen_sidlrt(lua_State *L) {
#if LUA_VERSION_NUM >= 503
  luaL_newlib(L, sidlrtlib);
  lua_setglobal(L, "sidlrt");
#else
  luaL_register(L, "sidlrt", sidlrtlib);
  lua_pop(L, 1);
#endif
}
