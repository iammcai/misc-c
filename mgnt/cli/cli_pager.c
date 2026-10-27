/*
 * Copyright (C) cai<sybstudy@yeah.net>. All rights reserved.
 *
 * @file    cli_pager.c
 * @brief   cli分页器实现
 *
 * @author  cai<sybstudy@yeah.net>
 * @date    2026-10-27
 * @version 1.0
 *
 * @note    
 *
 * @history
 *   1.0 | 2026-10-27 | cai | Initial creation.
 */

/* ========================================================================== */
/*                               Include Files                                */
/* ========================================================================== */

#include <stdio.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#include "cli/cli_pager.h"
#include "event/ev_lock.h"

/* ========================================================================== */
/*                             Macro Definitions                              */
/* ========================================================================== */

// 单次打印缓冲上限
#define CLI_PAGER_BUFFER_SIZE       (8192)

/* ========================================================================== */
/*                             Type Definitions                               */
/* ========================================================================== */



/* ========================================================================== */
/*                           Function Prototypes                              */
/* ========================================================================== */

/**
 * @brief       get an input key
 * 
 * @retval      key
 */
static int _cli_get_key(void)
{
    // 非交互环境（stdout 或 stdin 不是终端）不阻塞，直接返回换行
    if(!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
        return '\n';

    struct termios oldt, newt;
    int ch;

    if(tcgetattr(STDIN_FILENO, &oldt) != 0)
        return '\n';

    newt = oldt;

    // 关闭规范模式（无需回车）和回显（按键不显示在屏幕）
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // 用 read 直接读字节，避免 stdio 缓冲导致残留按键被“预读”
    do {
        ch = (int)read(STDIN_FILENO, &ch, 1);
    } while(ch < 0 && errno == EINTR);

    // 恢复终端属性
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    if(ch <= 0)
        return '\n';

    return ch;
}

/**
 * @brief       handle pause logic
 * 
 * @param[in]   ctx
 * 
 * @retval      1 - continue printf, 0 - quit
 *
 * @note        调用此函数时必须处于“行首”位置，提示符擦除才不会
 *              影响已经输出的内容。
 */
static int _cli_handle_pause(cli_ctx_t *ctx)
{
    // 非终端输出（重定向到文件、管道、串口日志等）不分页，
    // 也不打印任何提示符/转义序列，避免污染输出。
    if(!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
    {
        ctx->current_lines = 0;
        return 1;
    }

    // 打印反白高亮提示符
    printf("\033[7m -- More (q to quit, any key to continue) -- \033[0m");
    fflush(stdout);

    int key = _cli_get_key();       // 获取用于输入

    // 擦除提示符 (\r 回到行首, \033[K 清除到行尾)
    printf("\r\033[K");
    fflush(stdout);

    if (key == 'q' || key == 'Q') {
        ctx->is_aborted = 1; // 标记中断
        // 确保后续 CLI 提示符出现在新行，而不是接在半行输出后面
        printf("\n");
        fflush(stdout);
        return 0; 
    }

    // 其他任意键，重置计数器，继续下一页
    ctx->current_lines = 0;
    return 1;
}

/* ========================================================================== */
/*                             Global Variables                               */
/* ========================================================================== */

// extern打印使用的互斥锁
extern ev_mutex_t g_printf_mtx;

/* ========================================================================== */
/*                           Function Definition                              */
/* ========================================================================== */

void cli_ctx_init(cli_ctx_t *ctx)
{
    if(likely(ctx))
    {
        ctx->current_lines = 0;
        ctx->page_limit = CLI_PAGE_LINES_MAX;
        ctx->is_aborted = 0;
        ctx->data = NULL;
    }
}

void cli_ctx_cleanup(cli_ctx_t *ctx)
{
    if(likely(ctx))
    {
        ctx->is_aborted = 1;
    }
}

int cli_printf(cli_ctx_t *ctx, const char *fmt, ...)
{
    pfm_ensure_ret(ctx, -1);

    // 已退出则跳过打印
    if(ctx->is_aborted)
        return 0;

    int len = 0;
    char buffer[CLI_PAGER_BUFFER_SIZE] = {};
    va_list args;
    va_start(args, fmt);    // 获取...部分
    len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if(len <= 0)
        return len;
    else if((size_t)len >= sizeof(buffer))      // 截断保护
        len = sizeof(buffer) - 1;

    // 非终端输出：不分页、不计数、不打印提示符，直接原样输出。
    // 这样重定向到文件/管道/串口日志时输出干净，不会出现
    // "-- More --"、"\r\033[K" 等转义序列污染，也不会因擦除导致字符丢失。
    int interactive = isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);

    int aborted = 0;

    ev_with_mutex(&g_printf_mtx)
    {
        if(!interactive)
        {
            fwrite(buffer, 1, (size_t)len, stdout);
        }
        else
        {
            // 处理上一次调用遗留的分页状态：
            // 上一次输出末尾正好达到 page_limit，但为避免在行中间暂停，
            // 当时没有触发暂停。此时应已处于行首（最后输出的是 '\n'），
            // 在这里先暂停，然后再输出本次内容。
            if(ctx->current_lines >= ctx->page_limit)
            {
                if(!_cli_handle_pause(ctx))
                    aborted = 1;
            }

            if(!aborted)
            {
                for(int i = 0; i < len; ++ i)
                {
                    putchar(buffer[i]);         // 逐字节输出

                    // 只在换行后判断是否分页，确保暂停时一定处于行首，
                    // 避免 _cli_handle_pause() 的 "\r\033[K" 擦除本行已输出字符
                    if(buffer[i] == '\n')
                    {
                        ctx->current_lines ++;

                        if(ctx->current_lines >= ctx->page_limit)
                        {
                            if(!_cli_handle_pause(ctx))
                            {
                                aborted = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    fflush(stdout);     // 确保输出

    if(aborted)
        return -1;

    return len;
}