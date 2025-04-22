/*---------------------------------------------------------------------------------------------
 *  Copyright (c) 2024 - present handsomesnail
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

static SidlInstanceType GetSidlInstanceTypeByFieldType(SidlFieldType sidl_field_type) {
  switch (sidl_field_type) {
    case SidlFieldType::OBJECT:
      return SidlInstanceType::OBJECT;
    case SidlFieldType::ARRAY:
      return SidlInstanceType::ARRAY;
    case SidlFieldType::MAP:
      return SidlInstanceType::MAP;
    default:
      return SidlInstanceType::UNKNOWN;
  }
}

EXPORT void *CALL luaL_testudata(lua_State *L, int ud, const char *tname);

EXPORT int CALL xlua_issidlvalue(lua_State *L, int idx, SidlFieldType sidl_field_type);
EXPORT SidlValue CALL xlua_tosidlvalue(lua_State *L, int idx, SidlFieldType sidl_field_type);
EXPORT void CALL xlua_pushsidlvalue(lua_State *L, SidlFieldType sidl_field_type, SidlValue value);
EXPORT void CALL xlua_pushsidlobj(lua_State *L, uint64_t instance_id);

static int sidlrt_new_obj(lua_State *L);
static int sidlrt_invalidate(lua_State *L);
static int sidlrt_new_array(lua_State *L);
static int sidlrt_new_map(lua_State *L);

static int sidlobj_index(lua_State *L);
static int sidlobj_newindex(lua_State *L);

static int sidlarray_index(lua_State *L);
static int sidlarray_newindex(lua_State *L);
static int sidlarray_length(lua_State *L);
static int sidlarray_next(lua_State *L);
static int sidlarray_paris(lua_State *L);

static int sidlmap_index(lua_State *L);
static int sidlmap_newindex(lua_State *L);
static int sidlmap_length(lua_State *L);
static int sidlmap_next(lua_State *L);
static int sidlmap_paris(lua_State *L);

static int sidlinstance_gc(lua_State *L);
static int sidlinstance_tostring(lua_State *L);

static const char *tname_arr[5] = {"Unknown", "SidlObject", "SidlArray", "SidlMap", "SidlIter"};
static const lua_CFunction index_funcs[5] = {nullptr, sidlobj_index, sidlarray_index, sidlmap_index, nullptr};
static const lua_CFunction newindex_funcs[5] = {nullptr, sidlobj_newindex, sidlarray_newindex, sidlmap_newindex,
                                                nullptr};

EXPORT void CALL xlua_pushsidlvalue(lua_State *L, SidlFieldType sidl_field_type, SidlValue value) {
  if (sidl_field_type == SidlFieldType::INT || sidl_field_type == SidlFieldType::ENUM) {
    lua_pushinteger(L, value.intValue);
  } else if (sidl_field_type == SidlFieldType::LONG) {
    lua_pushint64(L, value.longValue);
  } else if (sidl_field_type == SidlFieldType::FLOAT) {
    lua_pushnumber(L, value.floatValue);
  } else if (sidl_field_type == SidlFieldType::DOUBLE) {
    lua_pushnumber(L, value.doubleValue);
  } else if (sidl_field_type == SidlFieldType::BOOL) {
    lua_pushboolean(L, value.booleanValue);
  } else if (sidl_field_type == SidlFieldType::STRING) {
    lua_pushstring(L, value.stringValue);
  } else if (sidl_field_type == SidlFieldType::OBJECT || sidl_field_type == SidlFieldType::ARRAY ||
             sidl_field_type == SidlFieldType::MAP) {
    xlua_pushsidlobj(L, value.objectInstanceId);
  }
}

EXPORT int CALL xlua_issidlvalue(lua_State *L, int idx, SidlFieldType sidl_field_type) {
  if (sidl_field_type == SidlFieldType::INT || sidl_field_type == SidlFieldType::ENUM ||
      sidl_field_type == SidlFieldType::LONG || sidl_field_type == SidlFieldType::FLOAT ||
      sidl_field_type == SidlFieldType::DOUBLE) {
    return lua_isnumber(L, idx);
  } else if (sidl_field_type == SidlFieldType::BOOL) {
    return lua_isboolean(L, idx);
  } else if (sidl_field_type == SidlFieldType::STRING) {
    return lua_isstring(L, idx);
  } else if (sidl_field_type == SidlFieldType::OBJECT || sidl_field_type == SidlFieldType::ARRAY ||
             sidl_field_type == SidlFieldType::MAP) {
    SidlInstanceType instanceType = GetSidlInstanceTypeByFieldType(sidl_field_type);
    const char *tname = tname_arr[static_cast<uint32_t>(instanceType)];
    if (lua_isnil(L, idx)) {
      return 1;
    } else if (lua_isuserdata(L, idx) && (luaL_testudata(L, idx, tname) != nullptr)) {
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}

EXPORT SidlValue CALL xlua_tosidlvalue(lua_State *L, int idx, SidlFieldType sidl_field_type) {
  SidlValue value = SidlValue();
  if (sidl_field_type == SidlFieldType::INT || sidl_field_type == SidlFieldType::ENUM) {
    value.intValue = lua_tointeger(L, idx);
  } else if (sidl_field_type == SidlFieldType::LONG) {
    value.longValue = lua_toint64(L, idx);
  } else if (sidl_field_type == SidlFieldType::FLOAT) {
    value.floatValue = lua_tonumber(L, idx);
  } else if (sidl_field_type == SidlFieldType::DOUBLE) {
    value.doubleValue = lua_tonumber(L, idx);
  } else if (sidl_field_type == SidlFieldType::BOOL) {
    value.booleanValue = lua_toboolean(L, idx);
  } else if (sidl_field_type == SidlFieldType::STRING) {
    value.stringValue = lua_tostring(L, idx);
  } else if (sidl_field_type == SidlFieldType::OBJECT || sidl_field_type == SidlFieldType::ARRAY ||
             sidl_field_type == SidlFieldType::MAP) {
    if (lua_isnil(L, idx)) {
      // 允许直接给obj赋nil值
      value.objectInstanceId = 0;
    } else if (lua_isuserdata(L, idx)) {
      // userdata类型且tname匹配
      value.objectInstanceId = *(uint64_t *)lua_touserdata(L, idx);
    } else {
      value.objectInstanceId = 0;
    }
  }
  return value;
}

EXPORT uint32_t CALL xlua_issidlobj(lua_State *L, int idx) {
  bool is_sidl_obj = lua_isuserdata(L, idx) &&
                     (luaL_testudata(L, idx, tname_arr[static_cast<uint32_t>(SidlInstanceType::OBJECT)]) != nullptr ||
                      luaL_testudata(L, idx, tname_arr[static_cast<uint32_t>(SidlInstanceType::ARRAY)]) != nullptr ||
                      luaL_testudata(L, idx, tname_arr[static_cast<uint32_t>(SidlInstanceType::MAP)]) != nullptr);
  return is_sidl_obj;
}

EXPORT uint32_t CALL xlua_checksidlobj(lua_State *L, int idx, SidlInstanceType insatnceType) {
  const char *tname = tname_arr[static_cast<uint32_t>(insatnceType)];
  return lua_isuserdata(L, idx) && (luaL_testudata(L, idx, tname) != nullptr);
}

EXPORT uint64_t CALL xlua_getsidlobj(lua_State *L, int idx) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, idx);
  return instance_id;
}

EXPORT void CALL xlua_pushsidlobj(lua_State *L, uint64_t instance_id) {
  if (instance_id == NULL_OBJECT_INSTANCE_ID) {
    lua_pushnil(L);
    return;
  }

  int top = lua_gettop(L);
  lua_getglobal(L, "global_sidlobj_cache");  // R(top+1): global_sidlobj_cache
  lua_pushuint64(L, instance_id);
  lua_gettable(L, top + 1);  // R(top+2): UserData

  if (lua_type(L, -1) == LUA_TNIL) {
    // 没有缓存，则创建新的UserData并放入global_sidlobj_cache
    lua_pop(L, 1);
    uint64_t *pointer = (uint64_t *)lua_newuserdata(L, sizeof(uint64_t));
    *pointer = instance_id;  // UserData只包含一个64位InstanceId
    lua_pushuint64(L, instance_id);
    lua_pushvalue(L, top + 2);
    lua_settable(L, top + 1);

    // 设置元表
    SidlInstanceType instanceType = SidlAPI_GetSidlInstanceType(instance_id);
    const char *tname = tname_arr[static_cast<uint32_t>(instanceType)];
    luaL_getmetatable(L, tname);  // R(top+3): UserData metatable
    if (lua_type(L, -1) == LUA_TNIL) {
      // 注册表中不存在名为tname的元表，则创建新的
      lua_pop(L, 1);
      luaL_newmetatable(L, tname);
      // 设置__index元方法
      lua_CFunction index_func = index_funcs[static_cast<uint32_t>(instanceType)];
      if (index_func != nullptr) {
        lua_pushcfunction(L, index_func);
        lua_setfield(L, top + 3, "__index");
      }
      // 设置__newindex元方法
      lua_CFunction newindex_func = newindex_funcs[static_cast<uint32_t>(instanceType)];
      if (newindex_func != nullptr) {
        lua_pushcfunction(L, newindex_func);
        lua_setfield(L, top + 3, "__newindex");
      }
      // 设置__pairs和__len元方法
      if (instanceType == SidlInstanceType::ARRAY) {
        lua_pushcfunction(L, sidlarray_paris);
        lua_setfield(L, top + 3, "__pairs");
        lua_pushcfunction(L, sidlarray_length);
        lua_setfield(L, top + 3, "__len");
      } else if (instanceType == SidlInstanceType::MAP) {
        lua_pushcfunction(L, sidlmap_paris);
        lua_setfield(L, top + 3, "__pairs");
        lua_pushcfunction(L, sidlmap_length);
        lua_setfield(L, top + 3, "__len");
      }
      // 设置__gc元方法
      lua_pushcfunction(L, sidlinstance_gc);
      lua_setfield(L, top + 3, "__gc");
      // 设置__tostring元方法
      lua_pushcfunction(L, sidlinstance_tostring);
      lua_setfield(L, top + 3, "__tostring");
    }
    lua_setmetatable(L, top + 2);

    // 引用计数+1
    SidlAPI_RetainObject(instance_id);
  }

  // 只保留UserData在栈顶
  lua_replace(L, top + 1);
  lua_settop(L, top + 1);
}

static int sidlobj_index(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Field name doesn't match string type");
  }
  const char *field_name = lua_tostring(L, 2);  // R(2): FieldName
  SidlFieldType sidl_field_type = SidlAPI_GetFieldType(instance_id, field_name);
  if (sidl_field_type == SidlFieldType::UNKNOWN) {
    return luaL_error(L, "The field \"%s\" doesn't exist", field_name);
  }
  SidlValue value = SidlAPI_GetFieldValue(instance_id, field_name);
  xlua_pushsidlvalue(L, sidl_field_type, value);  // Return(1): FieldValue
  return 1;
}

static int sidlobj_newindex(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Field name doesn't match string type");
  }
  const char *field_name = lua_tostring(L, 2);  // R(2): FieldName
  SidlFieldType sidl_field_type = SidlAPI_GetFieldType(instance_id, field_name);
  if (sidl_field_type == SidlFieldType::UNKNOWN) {
    return luaL_error(L, "The field \"%s\" doesn't exist", field_name);
  }
  int value_idx = 3;  // R(3): FieldValue
  if (!xlua_issidlvalue(L, value_idx, sidl_field_type)) {
    return luaL_error(L, "The field \"%s\" value doesn't match type", field_name);
  }
  SidlValue value = xlua_tosidlvalue(L, value_idx, sidl_field_type);
  SidlAPI_SetFieldValue(instance_id, field_name, value);
  return 0;
}

static int sidlarray_index(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "SidlArray index doesn't match int type");
  }
  int index = lua_tointeger(L, 2);  // R(2): Index
  int count = SidlAPI_GetArrayCount(instance_id);
  if (index < 0 || index >= count) {
    return luaL_error(L, "SidlArray index ArgumentOutOfRangeException");
  }
  SidlFieldType sidl_field_type = SidlAPI_GetArrayValueMetaType(instance_id);
  SidlValue value = SidlAPI_GetArrayItemValue(instance_id, index);
  xlua_pushsidlvalue(L, sidl_field_type, value);  // Return(1): Itemvalue
  return 1;
}

static int sidlarray_newindex(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "SidlArray index doesn't match int type");
  }
  int index = lua_tointeger(L, 2);  // R(2): Index
  int count = SidlAPI_GetArrayCount(instance_id);
  if (index < 0 || index >= count) {
    return luaL_error(L, "SidlArray index ArgumentOutOfRangeException");
  }
  SidlFieldType sidl_field_type = SidlAPI_GetArrayValueMetaType(instance_id);
  int value_idx = 3;
  if (!xlua_issidlvalue(L, value_idx, sidl_field_type)) {
    return luaL_error(L, "SidlArray item value doesn't match type");
  }
  SidlValue value = xlua_tosidlvalue(L, value_idx, sidl_field_type);  // R(3): Itemvalue
  SidlAPI_SetArrayItemValue(instance_id, index, value);
  return 0;
}

static int sidlarray_length(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  int count = SidlAPI_GetArrayCount(instance_id);
  lua_pushinteger(L, count);  // Return(1): Count
  return 1;
}

static int sidlarray_next(lua_State *L) {
  int index = lua_tointeger(L, lua_upvalueindex(1));                           // UpValue[1]: Index
  int version = lua_tointeger(L, lua_upvalueindex(2));                         // UpValue[2]: Version
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, lua_upvalueindex(3));  // UpValue[3]: UserData
  // 迭代过程中不允许修改
  if (version != SidlAPI_GetInstanceVersion(instance_id)) {
    return luaL_error(L, "SidlArray has been modified during iteration");
  }
  int count = SidlAPI_GetArrayCount(instance_id);
  // 判断迭代结束
  if (index >= count) {
    return 0;
  }
  // 压入当前迭代的键和值
  lua_pushinteger(L, index);  // Return(1): Index
  SidlFieldType sidl_field_type = SidlAPI_GetArrayValueMetaType(instance_id);
  SidlValue value = SidlAPI_GetArrayItemValue(instance_id, index);
  xlua_pushsidlvalue(L, sidl_field_type, value);  // Return(2): Itemvalue
  // 更新迭代器状态Index
  index++;
  lua_pushinteger(L, index);
  lua_replace(L, lua_upvalueindex(1));

  return 2;
}

static int sidlarray_paris(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  int index = 0;
  lua_pushinteger(L, index);  // push UpValue[1]: 迭代器初始index状态
  int version = SidlAPI_GetInstanceVersion(instance_id);
  lua_pushinteger(L, version);             // push UpValue[2]: 容器version
  lua_pushvalue(L, 1);                     // push UpValue[3]: UserData自身
  lua_pushcclosure(L, sidlarray_next, 3);  // Return(1): 将迭代器函数绑定到迭代器状态
  lua_pushvalue(L, 1);                     // Return(2): UserData自身
  lua_pushnil(L);                          // Return(3): 初始键nil
  return 3;
}

static int sidlmap_index(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  SidlFieldType key_type = SidlAPI_GetMapKeyMetaType(instance_id);
  if (!xlua_issidlvalue(L, 2, key_type)) {
    return luaL_error(L, "SidlMap key value doesn't match type");
  }
  SidlValue key = xlua_tosidlvalue(L, 2, key_type);  // R(2): Key
  if (!SidlAPI_GetMapContainsKey(instance_id, key)) {
    // 访问不存在的键直接报错，而不是返回nil，和C#行为一致
    return luaL_error(L, "SidlMap key not found");
  }
  SidlFieldType value_type = SidlAPI_GetMapValueMetaType(instance_id);
  SidlValue value = SidlAPI_GetMapItemValue(instance_id, key);
  xlua_pushsidlvalue(L, value_type, value);  // Return(1): value
  return 1;
}

static int sidlmap_newindex(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  SidlFieldType key_type = SidlAPI_GetMapKeyMetaType(instance_id);
  if (!xlua_issidlvalue(L, 2, key_type)) {
    return luaL_error(L, "SidlMap key value doesn't match type");
  }
  SidlValue key = xlua_tosidlvalue(L, 2, key_type);  // R(2): Key
  SidlFieldType value_type = SidlAPI_GetMapValueMetaType(instance_id);
  if (!xlua_issidlvalue(L, 3, value_type)) {
    return luaL_error(L, "SidlMap value value doesn't match type");
  }
  SidlValue value = xlua_tosidlvalue(L, 3, value_type);  // R(3): Value
  SidlAPI_SetMapItemValue(instance_id, key, value);
  return 0;
}

static int sidlmap_length(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  int count = SidlAPI_GetMapCount(instance_id);
  lua_pushinteger(L, count);  // Return(1): Count
  return 1;
}

static int sidlmap_next(lua_State *L) {
  uint64_t iter_instance_id = *(uint64_t *)lua_touserdata(L, lua_upvalueindex(1));  // UpValue[1]: Iter UserData
  int version = lua_tointeger(L, lua_upvalueindex(2));                              // UpValue[2]: Version
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, lua_upvalueindex(3));       // UpValue[3]: UserData
  // 迭代过程中不允许修改
  if (version != SidlAPI_GetInstanceVersion(instance_id)) {
    return luaL_error(L, "SidlMap has been modified during iteration");
  }
  // 判断迭代结束
  if (SidlAPI_IsMapIterEnd(iter_instance_id)) {
    return 0;
  }
  // 压入当前迭代的键和值
  SidlFieldType key_type = SidlAPI_GetMapKeyMetaType(instance_id);
  SidlValue key = SidlAPI_GetMapIterKey(iter_instance_id);
  xlua_pushsidlvalue(L, key_type, key);  // Return(1): Key
  SidlFieldType value_type = SidlAPI_GetMapValueMetaType(instance_id);
  SidlValue value = SidlAPI_GetMapIterValue(iter_instance_id);
  xlua_pushsidlvalue(L, value_type, value);  // Return(2): Value
  // 更新迭代器状态Iter
  SidlAPI_MoveMapIterNext(iter_instance_id);

  return 2;
}

static int sidlmap_paris(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  uint64_t iter_instance_id = SidlAPI_GetMapIter(instance_id);
  xlua_pushsidlobj(L, iter_instance_id);  // push UpValue[1]: 迭代器Iter UserData
  int version = SidlAPI_GetInstanceVersion(instance_id);
  lua_pushinteger(L, version);           // push UpValue[2]: 容器version
  lua_pushvalue(L, 1);                   // push UpValue[3]: UserData自身
  lua_pushcclosure(L, sidlmap_next, 3);  // Return(1): 将迭代器函数绑定到迭代器状态
  lua_pushvalue(L, 1);                   // Return(2): UserData自身
  lua_pushnil(L);                        // Return(3): 初始键nil
  return 3;
}

static int sidlinstance_gc(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  // 引用计数-1
  if (instance_id != NULL_OBJECT_INSTANCE_ID) {
    SidlAPI_ReleaseObject(instance_id);
  }
  return 0;
}

static int sidlinstance_tostring(lua_State *L) {
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  const char *str = SidlAPI_SerializeToJson(instance_id).stringValue;
  lua_pushstring(L, str);  // R(1): String
  return 1;
}

/// @brief 获取实例Id
static int sidlrt_get_instance_id(lua_State *L) {
  if (!xlua_issidlobj(L, 1)) {
    return luaL_error(L, "Sidl instance type doesn't match");
  }
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  lua_pushuint64(L, instance_id);                            // Return(1): InstanceId
  return 1;
}

/// @brief 获取SidlObject的meta名称
static int sidlrt_get_meta_name(lua_State *L) {
  if (!xlua_checksidlobj(L, 1, SidlInstanceType::OBJECT)) {
    return luaL_error(L, "SidlObject type doesn't match");
  }
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  const char *meta_name = SidlAPI_GetInstanceMetaName(instance_id).stringValue;
  lua_pushstring(L, meta_name);  // Return(1): TypeName
  return 1;
}

/// @brief 获取SidlModelMetaData(所有Sidl定义)
static int sidlrt_get_model_meta_data(lua_State *L) {
  uint64_t instance_id = SidlAPI_GetMetaDataObject();
  xlua_pushsidlobj(L, instance_id);  // Return(1): SidlModelMetaData
  return 1;
}

/// @brief 创建SidlObject
/// @example local obj = sidlrt.newobj("TK.ItemData")
static int sidlrt_new_obj(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "SidlObject type name doesn't match string type");
  }
  const char *type_name = lua_tostring(L, 1);  // R(1): TypeName
  uint64_t instance_id = SidlAPI_NewObject(type_name);
  xlua_pushsidlobj(L, instance_id);  // Return(1): UserData
  return 1;
}

/// @brief 设为无效(instance_id设为0)
/// @example sidlrt.invalidate(obj)
static int sidlrt_invalidate(lua_State *L) {
  *(uint64_t *)lua_touserdata(L, 1) = NULL_OBJECT_INSTANCE_ID;  // R(1): UserData
  return 0;
}

/// @brief 创建SidlArray
/// @example local array = sidlrt.newarray(Sidl.Int, "")
/// @example local array = sidlrt.newarray(Sidl.Object, "TK.ItemData")
static int sidlrt_new_array(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "SidlArray field type doesn't match int type");
  }
  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "SidlArray field type name doesn't match string type");
  }
  SidlFieldType field_type = static_cast<SidlFieldType>(lua_tointeger(L, 1));  // R(1): FieldType
  if (field_type == SidlFieldType::ARRAY || field_type == SidlFieldType::MAP) {
    return luaL_error(L, "SidlArray doesn't support nesting");
  }
  const char *type_name = lua_tostring(L, 2);  // R(2): TypeName
  uint64_t instance_id = SidlAPI_NewArray(field_type, type_name);
  xlua_pushsidlobj(L, instance_id);  // Return(1): UserData
  return 1;
}

/// @brief 创建SidlMap
/// @example local map = sidlrt.newmap(Sidl.Int, Sidl.String, "", "")
/// @example local map = sidlrt.newmap(Sidl.Enum, Sidl.Object, "TK.ItemType", "TK.ItemData")
static int sidlrt_new_map(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "SidlMap key type doesn't match int type");
  }
  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "SidlMap value type doesn't match int type");
  }
  if (!lua_isstring(L, 3)) {
    return luaL_error(L, "SidlMap key type name doesn't match string type");
  }
  if (!lua_isstring(L, 4)) {
    return luaL_error(L, "SidlMap value type name doesn't match string type");
  }
  SidlFieldType key_type = static_cast<SidlFieldType>(lua_tointeger(L, 1));    // R(1): KeyType
  SidlFieldType value_type = static_cast<SidlFieldType>(lua_tointeger(L, 2));  // R(2): ValueType
  if (key_type == SidlFieldType::ARRAY || key_type == SidlFieldType::MAP || key_type == SidlFieldType::OBJECT ||
      value_type == SidlFieldType::ARRAY || value_type == SidlFieldType::MAP) {
    return luaL_error(L, "SidlMap doesn't support nesting");
  }
  const char *key_type_name = lua_tostring(L, 3);    // R(3): KeyTypeName
  const char *value_type_name = lua_tostring(L, 4);  // R(4): ValueTypeName
  uint64_t instance_id = SidlAPI_NewMap(key_type, value_type, key_type_name, value_type_name);
  xlua_pushsidlobj(L, instance_id);  // Return(1): UserData
  return 1;
}

static int sidlrt_resize_array(lua_State *L) {
  if (!xlua_checksidlobj(L, 1, SidlInstanceType::ARRAY)) {
    return luaL_error(L, "SidlArray type doesn't match");
  }
  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "SidlArray size type doesn't match int type");
  }
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  int size = lua_tointeger(L, 2);                            // R(2): Size
  SidlAPI_ResizeArray(instance_id, size);
  return 0;
}

static int sidlrt_clear_map(lua_State *L) {
  if (!xlua_checksidlobj(L, 1, SidlInstanceType::MAP)) {
    return luaL_error(L, "SidlMap type doesn't match");
  }
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  SidlAPI_ClearMap(instance_id);
  return 0;
}

static int sidlrt_contains_map_key(lua_State *L) {
  if (!xlua_checksidlobj(L, 1, SidlInstanceType::MAP)) {
    return luaL_error(L, "SidlMap type doesn't match");
  }
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  SidlFieldType key_type = SidlAPI_GetMapKeyMetaType(instance_id);
  if (!xlua_issidlvalue(L, 2, key_type)) {
    return luaL_error(L, "SidlMap key value doesn't match type");
  }
  SidlValue key = xlua_tosidlvalue(L, 2, key_type);                 // R(2): Key
  lua_pushboolean(L, SidlAPI_GetMapContainsKey(instance_id, key));  // Return(1): contains
  return 1;
}

static int sidlrt_remove_map_key(lua_State *L) {
  if (!xlua_checksidlobj(L, 1, SidlInstanceType::MAP)) {
    return luaL_error(L, "SidlMap type doesn't match");
  }
  uint64_t instance_id = *(uint64_t *)lua_touserdata(L, 1);  // R(1): UserData
  SidlFieldType key_type = SidlAPI_GetMapKeyMetaType(instance_id);
  if (!xlua_issidlvalue(L, 2, key_type)) {
    return luaL_error(L, "SidlMap key value doesn't match type");
  }
  SidlValue key = xlua_tosidlvalue(L, 2, key_type);  // R(2): Key
  if (SidlAPI_GetMapContainsKey(instance_id, key)) {
    SidlAPI_RemoveMapKey(instance_id, key);
    lua_pushboolean(L, 1);  // Return(1): true
  } else {
    lua_pushboolean(L, 0);  // Return(1): false
  }
  return 1;
}

static const luaL_Reg sidlrtlib[] = {{"getinstanceid", sidlrt_get_instance_id},
                                     {"getmetaname", sidlrt_get_meta_name},
                                     {"getmodelmetadata", sidlrt_get_model_meta_data},
                                     {"newobj", sidlrt_new_obj},
                                     {"invalidate", sidlrt_invalidate},
                                     {"newarray", sidlrt_new_array},
                                     {"newmap", sidlrt_new_map},
                                     {"resizearray", sidlrt_resize_array},
                                     {"clearmap", sidlrt_clear_map},
                                     {"containsmapkey", sidlrt_contains_map_key},
                                     {"removemapkey", sidlrt_remove_map_key},
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
