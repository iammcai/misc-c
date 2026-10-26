/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    json_util.c
 * @brief   json工具实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-26
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-10-26 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <string.h>
#include <stdio.h>

#include "json_util/json_util.h"
#include "mp/mp.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// json string初始分配大小
#define JSON_BUF_INIT_SIZE  (256)

// json最大嵌套深度
#define JSON_MAX_DEPTH      (64)

// 临时缓冲长度
#define JSON_TEMP_STR_SIZE  (64)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */

// json字符串定义
typedef struct{
    char *data;         // 内容，动态分配
    size_t len;         // 当前实际字节数
    size_t cap;         // 当前分配容量
}json_str_buf_t;

// 上下文状态栈帧，用于自动处理逗号
typedef struct{
    char type;      // '{' 为 object，'[' 为 array
    int count;      // 当前添加元素个数
}json_stack_frame_t;

// json builder定义
struct json_builder_s{
    json_str_buf_t buf;
    json_stack_frame_t stack[JSON_MAX_DEPTH];
    int depth;
    error_code_e error;
};

/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

//
/**
 * @brief       append str into json builder
 * 
 * @param[in]   jb
 * @param[in]   str     string to append
 * @param[in]   len     length of str
 * 
 * @note        追加内容，包含data扩容
 */
static void _json_buf_append(json_builder_t *jb, const char *str, size_t len)
{
    pfm_ensure_ret_void(jb && jb->error == ERR_NO_ERROR && len > 0);

    // 扩容：按 1.5 倍循环增长，直到一次追加能完全容纳
    size_t need = jb->buf.len + len + 1;
    if(need > jb->buf.cap)
    {
        size_t new_cap = jb->buf.cap;
        while(new_cap < need)
            new_cap += (new_cap >> 1);

        char *new_data = mp_realloc(jb->buf.data, new_cap, jb->buf.cap);
        if(unlikely(!new_data))
        {
            jb->error = ERR_NO_MEM;
            return;
        }
        jb->buf.data = new_data;
        jb->buf.cap  = new_cap;
    }

    // 复制到目标位置
    memcpy(jb->buf.data + jb->buf.len, str, len);
    jb->buf.len += len;
    jb->buf.data[jb->buf.len] = '\0';
}

/**
 * @brief       append char type value into jb
 */
static attr_force_inline void _json_buf_append_char(json_builder_t *jb, char ch)
{
    _json_buf_append(jb, &ch, 1);
}

/**
 * @brief       append separator into jb
 */
static attr_force_inline void _json_write_separator(json_builder_t *jb)
{
    if(jb->depth <= 0)      // 没有嵌套，直接返回
        return;
    json_stack_frame_t *frame = &jb->stack[jb->depth - 1];
    if(frame->count > 0)
        _json_buf_append_char(jb, ','); // 如果前面有数据，那么加一个,
    frame->count ++;
}

/**
 * @brief       append string with escaped, ""
 * 
 * @param[in]   jb
 * @param[in]   str     - string
 */
static attr_force_inline void _json_write_escaped_string(json_builder_t *jb, const char *str)
{
    if(!str)
    {
        _json_buf_append(jb, "null", strlen("null"));
        return;
    }
    
    _json_buf_append_char(jb, '"');
    for(const char *p = str; *p; p++)
    {
        switch(*p)
        {
            case '"': _json_buf_append(jb, "\\\"", 2);  break;
            case '\\': _json_buf_append(jb, "\\\\", 2); break;
            case '\n': _json_buf_append(jb, "\\n", 2);  break;
            case '\r': _json_buf_append(jb, "\\r", 2);  break;
            case '\t': _json_buf_append(jb, "\\t", 2);  break;
            default:    _json_buf_append_char(jb, *p);  break;
        }
    }
    _json_buf_append_char(jb, '"');
}

/**
 * @brief       add key, e.g. "key":
 * 
 * @param[in]   jb
 * @param[in]   key
 *
 * @note        内部会完成 separator 处理，因此 _with_key 版本
 *              不应再调用 json_object_start / json_array_start
 *              中带 separator 的逻辑，避免多逗号
 */
static attr_force_inline void _json_add_key(json_builder_t *jb, const char *key)
{
    pfm_ensure_ret_void(jb);
    if(jb->error != ERR_NO_ERROR)
        return;

    if(!key)
    {
        jb->error = ERR_BAD_PARAM;
        return;
    }

    // key必须在{}中
    if(jb->depth == 0 || jb->stack[jb->depth-1].type != '{')
    {
        jb->error = ERR_JSON_FRAME_TYPE_MISMATCH;
        return;
    }

    _json_write_separator(jb);
    if(jb->error != ERR_NO_ERROR)
        return;

    _json_write_escaped_string(jb, key);
    if(jb->error != ERR_NO_ERROR)
        return;

    _json_buf_append_char(jb, ':');
}

/**
 * @brief       push a new frame without writing separator
 *
 * @param[in]   jb
 * @param[in]   type    '{' or '['
 *
 * @note        仅负责追加 '{'/'[' 并压栈，不处理逗号。
 *              json_object_start / json_array_start 调用前会自行
 *              调 _json_write_separator；_with_key 版本则依赖
 *              _json_add_key 已经处理过 separator。
 */
static attr_force_inline void _json_frame_push(json_builder_t *jb, char type)
{
    if(jb->depth >= JSON_MAX_DEPTH)
    {
        jb->error = ERR_JSON_DEPTH_MAX;
        return;
    }

    _json_buf_append_char(jb, type);
    if(jb->error != ERR_NO_ERROR)
        return;

    jb->stack[jb->depth].type  = type;
    jb->stack[jb->depth].count = 0;
    jb->depth ++;
}

/**
 * @brief       common check for json array add
 * 
 * @param[in]   jb
 * 
 * @note        在json数组中添加元素的通用检查
 */
static attr_force_inline error_code_e _json_array_add_pre_check(json_builder_t* jb)
{
    pfm_ensure_ret(jb && ERR_NO_ERROR == jb->error, ERR_BAD_PARAM);
    if(jb->depth == 0 || jb->stack[jb->depth-1].type != '[')
        jb->error = ERR_JSON_FRAME_TYPE_MISMATCH;

    return jb->error;
}

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

json_builder_t* json_create(void)
{
    json_builder_t *jb = (json_builder_t*)mp_calloc(1, sizeof(json_builder_t));
    if(unlikely(!jb))
    {
        dbg_error("no memory for json_builder");
        return NULL;
    }

    jb->buf.data = mp_calloc(1, JSON_BUF_INIT_SIZE);
    if(unlikely(!jb->buf.data))
    {
        dbg_error("no memory for json_builder");
        mp_free(jb, sizeof(json_builder_t));
        return NULL;
    }
    jb->buf.data[0] = '\0';
    jb->buf.cap = JSON_BUF_INIT_SIZE;
    jb->buf.len = 0;

    jb->depth = 0;
    jb->error = ERR_NO_ERROR;

    return jb;
}

void json_destroy(json_builder_t *jb)
{
    if(likely(jb))
    {
        mp_free(jb->buf.data, jb->buf.cap);
        mp_free(jb, sizeof(json_builder_t));
    }
}

const char* json_string_get(const json_builder_t *jb)
{
    return jb ? jb->buf.data : "";
}

size_t json_length_get(const json_builder_t *jb)
{
    return jb ? jb->buf.len : 0;
}

int json_has_error(const json_builder_t *jb)
{
    return jb ? !jb->error : 1;
}

void json_object_start(json_builder_t *jb)
{
    pfm_ensure_ret_void(jb && jb->error == ERR_NO_ERROR);

    _json_write_separator(jb);          // 添加分隔符号（array 元素场景需要）
    _json_frame_push(jb, '{');          // 添加 {
}

void json_object_end(json_builder_t *jb)
{
    pfm_ensure_ret_void(jb && jb->error == ERR_NO_ERROR);
    if(jb->depth == 0)
    {
        jb->error = ERR_JSON_END_WITHOUT_START;
        return;
    }
    else if(jb->stack[jb->depth-1].type != '{')
    {
        jb->error = ERR_JSON_FRAME_TYPE_MISMATCH;
        return;
    }

    jb->depth --;
    _json_buf_append_char(jb, '}');     // 添加}
}

void json_array_start(json_builder_t *jb)
{
    pfm_ensure_ret_void(jb && jb->error == ERR_NO_ERROR);

    _json_write_separator(jb);          // 添加分隔符号（array 元素场景需要）
    _json_frame_push(jb, '[');          // 添加 [
}

void json_array_end(json_builder_t *jb)
{
    pfm_ensure_ret_void(jb && jb->error == ERR_NO_ERROR);
    if(jb->depth == 0)
    {
        jb->error = ERR_JSON_END_WITHOUT_START;
        return;
    }
    else if(jb->stack[jb->depth-1].type != '[')
    {
        jb->error = ERR_JSON_FRAME_TYPE_MISMATCH;
        return;
    }

    jb->depth --;
    _json_buf_append_char(jb, ']');     // 添加]
}

void json_object_start_with_key(json_builder_t *jb, const char *key)
{
    _json_add_key(jb, key);             // 这里已完成 separator 与 count++
    if(!jb || jb->error != ERR_NO_ERROR)
        return;
    _json_frame_push(jb, '{');          // 不再调 separator，避免多逗号
}

void json_array_start_with_key(json_builder_t *jb, const char *key)
{
    _json_add_key(jb, key);             // 这里已完成 separator 与 count++
    if(!jb || jb->error != ERR_NO_ERROR)
        return;
    _json_frame_push(jb, '[');          // 不再调 separator，避免多逗号
}

void json_add_string(json_builder_t *jb, const char *key, const char *val)
{
    _json_add_key(jb, key);
    if(!jb || jb->error != ERR_NO_ERROR)
        return;
    _json_write_escaped_string(jb, val);
}

void json_add_int(json_builder_t *jb, const char *key, long long val)
{
    _json_add_key(jb, key);
    if(!jb || jb->error != ERR_NO_ERROR)
        return;

    char num[JSON_TEMP_STR_SIZE];
    int len = snprintf(num, sizeof(num), "%lld", val);
    if(len > 0)
        _json_buf_append(jb, num, (size_t)len);
}

void json_add_double(json_builder_t *jb, const char *key, double val)
{
    _json_add_key(jb, key);
    if(!jb || jb->error != ERR_NO_ERROR)
        return;

    char num[JSON_TEMP_STR_SIZE];
    int len = snprintf(num, sizeof(num), "%g", val);
    if(len > 0)
        _json_buf_append(jb, num, (size_t)len);
}

void json_add_null(json_builder_t *jb, const char *key)
{
    _json_add_key(jb, key);
    if(!jb || jb->error != ERR_NO_ERROR)
        return;
    _json_buf_append(jb, "null", strlen("null"));
}

void json_array_add_string(json_builder_t *jb, const char *val)
{
    pfm_ensure_ret_void(ERR_NO_ERROR == _json_array_add_pre_check(jb));
    _json_write_separator(jb);
    _json_write_escaped_string(jb, val);
}

void json_array_add_int(json_builder_t *jb, long long val)
{
    pfm_ensure_ret_void(ERR_NO_ERROR == _json_array_add_pre_check(jb));
    _json_write_separator(jb);

    char num[JSON_TEMP_STR_SIZE];
    int len = snprintf(num, sizeof(num), "%lld", val);
    if(len > 0)
        _json_buf_append(jb, num, (size_t)len);
}

void json_array_add_double(json_builder_t *jb, double val)
{
    pfm_ensure_ret_void(ERR_NO_ERROR == _json_array_add_pre_check(jb));
    _json_write_separator(jb);

    char num[JSON_TEMP_STR_SIZE];
    int len = snprintf(num, sizeof(num), "%g", val);
    if(len > 0)
        _json_buf_append(jb, num, (size_t)len);
}

void json_array_add_null(json_builder_t *jb)
{
    pfm_ensure_ret_void(ERR_NO_ERROR == _json_array_add_pre_check(jb));
    _json_write_separator(jb);
    _json_buf_append(jb, "null", strlen("null"));
}