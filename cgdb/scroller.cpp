/* scroller.c:
 * -----------
 *
 * A scrolling buffer utility.  Able to add and subtract to the buffer.
 * All routines that would require a screen update will automatically refresh
 * the scroller.
 */

/* Local Includes */
#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* System Includes */
#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

/* Local Includes */
#include "sys_util.h"
#include "stretchy.h"
#include "sys_win.h"
#include "cgdb.h"
#include "cgdbrc.h"
#include "highlight_groups.h"
#include "scroller.h"
#include "highlight.h"
#include "vterminal.h"

static void
terminal_write_cb(char *buffer, size_t size, void *data)
{
    struct scroller *scroller = (struct scroller*)data;
    // TODO: ?
}

static void
terminal_resize_cb(int width, int height, void *data)
{
    struct scroller *scroller = (struct scroller*)data;
    // TODO: ?
}

static void
terminal_close_cb(void *data)
{
    struct scroller *scroller = (struct scroller*)data;
    // TODO: ?
}

/* ----------------- */
/* Exposed Functions */
/* ----------------- */

/* See scroller.h for function descriptions. */

struct scroller *scr_new(SWINDOW *win)
{
    struct scroller *rv;

    rv = (struct scroller *)cgdb_malloc(sizeof(struct scroller));

    rv->in_scroll_mode = false;
    rv->scroll_cursor_row = rv->scroll_cursor_col = 0;
    rv->win = win;

    rv->in_search_mode = false;
    rv->last_hlregex = NULL;
    rv->hlregex = NULL;
    rv->search_row = rv->search_col_start = rv->search_col_end = 0;

    rv->jump_back_mark.r = -1;
    rv->jump_back_mark.c = -1;
    memset(rv->marks, 0xff, sizeof(rv->marks));

    VTerminalOptions options;
    options.data = (void*)rv;
    options.width = swin_getmaxx(rv->win);
    options.height = swin_getmaxy(rv->win);
    options.terminal_write_cb = terminal_write_cb;
    options.terminal_resize_cb = terminal_resize_cb;
    options.terminal_close_cb = terminal_close_cb;

    rv->vt = vterminal_new(options);

    return rv;
}

void scr_free(struct scroller *scr)
{
    int i;

    vterminal_free(scr->vt);

    hl_regex_free(&scr->last_hlregex);
    scr->last_hlregex = NULL;
    hl_regex_free(&scr->hlregex);
    scr->hlregex = NULL;

    swin_delwin(scr->win);
    scr->win = NULL;

    /* Release the scroller object */
    free(scr);
}

void scr_set_scroll_mode(struct scroller *scr, bool mode)
{
    // If the request is to enable the scroll mode and it's not already 
    // enabled, then enable it
    if (mode && !scr->in_scroll_mode) {
        scr->in_scroll_mode = true;
        // Start the scroll mode cursor at the same location as the 
        // cursor on the screen
        vterminal_get_cursor_pos(
                scr->vt, scr->scroll_cursor_row, scr->scroll_cursor_col);
    // If the request is to disable the scroll mode and it's currently
    // enabled, then disable it
    } else if (!mode && scr->in_scroll_mode) {
        scr->in_scroll_mode = false;
    }
}

void scr_up(struct scroller *scr, int nlines)
{
    // When moving 1 line up
    //   Move the cursor towards the top of the screen
    //   If it hits the top, then start scrolling back
    // Otherwise whem moving many lines up, simply scroll
    if (scr->scroll_cursor_row > 0 && nlines == 1) {
        scr->scroll_cursor_row = scr->scroll_cursor_row - 1;
    } else {
        vterminal_scroll_delta(scr->vt, nlines);
    }
}

void scr_down(struct scroller *scr, int nlines)
{
    int height;
    int width;
    vterminal_get_height_width(scr->vt, height, width);

    // When moving 1 line down
    //   Move the cursor towards the botttom of the screen
    //   If it hits the botttom, then start scrolling forward
    // Otherwise whem moving many lines down, simply scroll
    if (scr->scroll_cursor_row < height - 1 && nlines == 1) {
        scr->scroll_cursor_row = scr->scroll_cursor_row + 1;
    } else {
        vterminal_scroll_delta(scr->vt, -nlines);
    }
}

void scr_home(struct scroller *scr)
{
    int sb_num_rows;
    vterminal_get_sb_num_rows(scr->vt, sb_num_rows);
    vterminal_scroll_delta(scr->vt, sb_num_rows);
}

void scr_end(struct scroller *scr)
{
    int sb_num_rows;
    vterminal_get_sb_num_rows(scr->vt, sb_num_rows);
    vterminal_scroll_delta(scr->vt, -sb_num_rows);
}

void scr_left(struct scroller *scr)
{
    if (scr->scroll_cursor_col > 0) {
        scr->scroll_cursor_col--;
    }
}

void scr_right(struct scroller *scr)
{
    int height;
    int width;
    vterminal_get_height_width(scr->vt, height, width);

    if (scr->scroll_cursor_col < width - 1) {
        scr->scroll_cursor_col++;
    }
}

void scr_beginning_of_row(struct scroller *scr)
{
    scr->scroll_cursor_col = 0;
}

void scr_end_of_row(struct scroller *scr)
{
    int height;
    int width;
    vterminal_get_height_width(scr->vt, height, width);

    scr->scroll_cursor_col = width - 1;
}

void scr_push_screen_to_scrollback(struct scroller *scr)
{
    vterminal_push_screen_to_scrollback(scr->vt);
}

void scr_add(struct scroller *scr, const char *buf)
{
    // Keep a copy of all text sent to vterm
    // Vterm doesn't yet support resizing, so we would create a new vterm
    // instance and feed it the same data
    scr->text.append(buf);

    vterminal_push_bytes(scr->vt, buf, strlen(buf));
}

void scr_move(struct scroller *scr, SWINDOW *win)
{
    swin_delwin(scr->win);
    scr->win = win;

    // recreate the vterm session with the new size
    vterminal_free(scr->vt);

    VTerminalOptions options;
    options.data = (void*)scr;
    options.width = swin_getmaxx(scr->win);
    options.height = swin_getmaxy(scr->win);
    options.terminal_write_cb = terminal_write_cb;
    options.terminal_resize_cb = terminal_resize_cb;
    options.terminal_close_cb = terminal_close_cb;

    scr->vt = vterminal_new(options);
    vterminal_push_bytes(scr->vt, scr->text.data(), scr->text.size());
}

static int scr_search_regex_forward(struct scroller *scr, const char *regex,
    int opt, int icase)
{
    int sb_num_rows;
    vterminal_get_sb_num_rows(scr->vt, sb_num_rows);

    int height;
    int width;
    vterminal_get_height_width(scr->vt, height, width);

    int delta;
    vterminal_scroll_get_delta(scr->vt, delta);

    int wrapscan_enabled = cgdbrc_get_int(CGDBRC_WRAPSCAN);

    int count = sb_num_rows + height;
    int regex_matched;

    if (!scr || !regex) {
        // TODO: LOG ERROR
        return 0;
    }

    // The starting search row and column
    int search_row = scr->search_sid_init;
    int search_col = scr->search_col_init;

    // Increment the column by 1 to get the starting row/column
    if (search_col < width - 1) {
        search_col++;
    } else {
        search_row++;
        if (search_row >= count) {
            search_row = 0;
        }
        search_col = 0;
    }

    for (;;)
    {
        int start, end;
        // convert from sid to cursor position taking into account delta
        int vfr = search_row - sb_num_rows + delta;
        char *utf8buf;
        int attr;
        vterminal_fetch_row(scr->vt, vfr, search_col, width, utf8buf, attr);
        regex_matched = hl_regex_search(&scr->hlregex, utf8buf, regex,
                icase, &start, &end);
        if (regex_matched > 0) {
            // Need to scroll the terminal if the search is not in view
            if (count - delta - height <= search_row &&
                search_row < count - delta) {
            } else {
                delta = search_row - sb_num_rows;
                if (delta > 0) {
                    delta = 0;
                }
                delta = -delta;
                vterminal_scroll_set_delta(scr->vt, delta);
            }

            // convert from sid to cursor position taking into account delta
            scr->search_row = search_row - sb_num_rows + delta;
            scr->search_col_start = start + search_col;
            scr->search_col_end = end + search_col;
            break;
        }

        // Stop searching when made it back to original position
        if (wrapscan_enabled &&
            search_row == scr->search_sid_init && search_col == 0) {
            break;
        // Or if wrapscan is disabled and searching hit the end
        } else if (!wrapscan_enabled && search_row == count - 1) {
            break;
        }

        search_row++;
        if (search_row >= count) {
            search_row = 0;
        }
        search_col = 0;
    }

    /* Finalized match - move to this location or roll back to previous */
    if (opt == 2) {
        if (regex_matched) {
            scr->scroll_cursor_row = scr->search_row;
            scr->scroll_cursor_col = scr->search_col_start;

            // TODO: Can we move delta location moving above down here?

            hl_regex_free(&scr->hlregex);
            scr->last_hlregex = scr->hlregex;
            scr->hlregex = 0;
        } else {
            vterminal_scroll_set_delta(scr->vt, scr->delta_init);
        }

        scr->search_row = 0;
        scr->search_col_start = 0;
        scr->search_col_end = 0;
    }

    return regex_matched;
}

static int scr_search_regex_backwards(struct scroller *scr, const char *regex,
    int opt, int icase)
{
    int sb_num_rows;
    vterminal_get_sb_num_rows(scr->vt, sb_num_rows);

    int height;
    int width;
    vterminal_get_height_width(scr->vt, height, width);

    int delta;
    vterminal_scroll_get_delta(scr->vt, delta);

    int wrapscan_enabled = cgdbrc_get_int(CGDBRC_WRAPSCAN);

    int count = sb_num_rows + height;
    int regex_matched = 0;

    if (!scr || !regex) {
        // TODO: LOG ERROR
        return 0;
    }

    // The starting search row and column
    int search_row = scr->search_sid_init;
    int search_col = scr->search_col_init;

    // Decrement the column by 1 to get the starting row/column
    if (search_col > 0) {
        search_col--;
    } else {
        search_row--;
        if (search_row < 0) {
            search_row = count - 1;
        }
        search_col = width - 1;
    }

    for (;;)
    {
        int start = 0, end = 0;
        int vfr = search_row - sb_num_rows + delta;

        // Searching in reverse is more difficult
        // The idea is to search right to left, however the regex api
        // doesn't support that. Need to mimic this by searching left
        // to right to find all the matches on the line, and then 
        // take the right most match.
        for (int c = 0;;) {
            char *utf8buf;
            int attr;
            vterminal_fetch_row(scr->vt, vfr, c, width, utf8buf, attr);

            int _start, _end, result;
            result = hl_regex_search(&scr->hlregex, utf8buf, regex, icase,
                    &_start, &_end);
            if ((result == 1) && (c + _start <= search_col)) {
                regex_matched = 1;
                start = c + _start;
                end = c + _end;
                c = start + 1;
            } else {
                break;
            }
        }

        if (regex_matched > 0) {
            // Need to scroll the terminal if the search is not in view
            if (count - delta - height <= search_row &&
                search_row < count - delta) {
            } else {
                delta = search_row - sb_num_rows;
                if (delta > 0) {
                    delta = 0;
                }
                delta = -delta;
                vterminal_scroll_set_delta(scr->vt, delta);
            }

            scr->search_row = search_row - sb_num_rows + delta;
            scr->search_col_start = start;
            scr->search_col_end = end;
            break;
        }

        // Stop searching when made it back to original position
        if (wrapscan_enabled &&
            search_row == scr->search_sid_init &&
            search_col == width - 1) {
            int silly;
            silly = 1;
            break;
        // Or if wrapscan is disabled and searching hit the top
        } else if (!wrapscan_enabled && search_row == 0) {
            int silly;
            silly = 1;
            break;
        }

        search_row--;
        if (search_row < 0) {
            search_row = count - 1;
        }
        search_col = width - 1;
    }

    /* Finalized match - move to this location */
    if (opt == 2) {
        if (regex_matched) {
            scr->scroll_cursor_row = scr->search_row;
            scr->scroll_cursor_col = scr->search_col_start;

            hl_regex_free(&scr->hlregex);
            scr->last_hlregex = scr->hlregex;
            scr->hlregex = 0;
        } else {
            vterminal_scroll_set_delta(scr->vt, scr->delta_init);
        }

        scr->search_row = 0;
        scr->search_col_start = 0;
        scr->search_col_end = 0;
    }

    return regex_matched;
}

int scr_search_regex(struct scroller *scr, const char *regex, int opt,
    int direction, int icase)
{
    // Some help understanding how searching in the scroller works
    //
    // - Vterm deals only with what's on the screen
    //   It represents rows 0 through vterm height-1, which is 2 below
    // - vterminal introduces a scrollback buffer
    //   It represents rows -1 through -scrollback height, which is -6 below
    // - vterminal also introduces a scrollback delta
    //   Allows iterating from 0:height-1 but displaying the scrolled to text
    //   The default is 0, which is represented by d0
    //   Scrolling back all the way to -6 is represented by d-6
    //   Scrolling back partiall to -2 is represented by d-2
    // - The scroller has introduced the concept of a search id (sid)
    //   The purpose is to iterate easily over all the text (vterm+scrollback)
    //
    // Example inputs and labels
    //   Screen Height: 3
    //   Scrollback (sb) size: 6
    //   vid: VTerm ID (screen only)
    //   tid: Terminal ID (screen + scrollback + scrollback delta)
    //   sid: A search ID (for iterating eaisly over all)
    //   sb start - scrollback buffer start
    //   sb end - scrollback buffer end
    //   vt start - vterm buffer start
    //   vt end - vterm buffer end
    // 
    //          sid vid tid d0 d-6 d-2
    // sb start  0      -6       0      abc     0
    //           1      -5       1         def  1
    //           2      -4       2      ghi     2
    //           3      -3                 def   
    //           4      -2           0  jkl
    // sb end    5      -1           1     def
    // vt start  6   0  0    0       2  mno
    //           7   1  1    1             def
    // vt end    8   2  2    2          pqr
    //
    // Your search will start at the row the scroll cursor is at.
    //
    // You can loop from 0 to scrollback size + vterm size.
    //
    // You can convert your cursor position to the sid by doing:
    //   sid = cursor_pos + scrollback size + vterminal_scroll_get_delta
    // You can convert your sid to a cursor position by doing the following:
    //   cursor_pos = sid - scrollback size - vterminal_scroll_get_delta
    //
    // If your delta is -6, and your cursor is on sid 1, and you find a
    // match on sid 7, you'll have to move the display by moving the delta.
    // You can move the display to sid by doing the following:
    //   delta_offset = sid - scrollback size
    // Then, if delta_offset > 0, delta_offset = 0.
    int result;

    if (direction) {
        result = scr_search_regex_forward(scr, regex, opt, icase);
    } else {
        result = scr_search_regex_backwards(scr, regex, opt, icase);
    }

    return result;
}

void scr_search_regex_init(struct scroller *scr)
{
    int delta;
    vterminal_scroll_get_delta(scr->vt, delta);

    int sb_num_rows;
    vterminal_get_sb_num_rows(scr->vt, sb_num_rows);

    scr->in_search_mode = true;
    scr->delta_init = delta;
    scr->search_sid_init = scr->scroll_cursor_row - delta + sb_num_rows;
    scr->search_col_init = scr->scroll_cursor_col;
}

void scr_search_regex_final(struct scroller *scr)
{
    scr->in_search_mode = false;
    vterminal_scroll_set_delta(scr->vt, scr->delta_init);
}

int scr_set_mark(struct scroller *scr, int key)
{
    int cursor_row, cursor_col;
    vterminal_get_cursor_pos(scr->vt, cursor_row, cursor_col);

    if (key >= 'a' && key <= 'z')
    {
        /* Local buffer mark */
        scr->marks[key - 'a'].r = cursor_row;
        scr->marks[key - 'a'].c = cursor_col;
        return 1;
    }

    return 0;
}

int scr_goto_mark(struct scroller *scr, int key)
{
#if 0
    scroller_mark mark_temp;
    scroller_mark *mark = NULL;

    if (key >= 'a' && key <= 'z')
    {
        /* Local buffer mark */
        mark = &scr->marks[key - 'a'];
    }
    else if (key == '\'')
    {
        /* Jump back to where we last jumped from */
        mark_temp = scr->jump_back_mark;
        mark = &mark_temp;
    }
    else if (key == '.')
    {
        /* Jump to last line */
        mark_temp.r = sbcount(scr->lines) - 1;
        mark_temp.c = get_last_col(scr, scr->current.r);
        mark = &mark_temp;
    }

    if (mark && (mark->r >= 0))
    {
        int cursor_row, cursor_col;
        vterminal_get_cursor_pos(scr->vt, cursor_row, cursor_col);

        scr->jump_back_mark.r = scr->current.r;
        scr->jump_back_mark.c = scr->current.c;

        scr->current.r = mark->r;
        scr->current.c = mark->c;
        return 1;
    }

#endif
    return 0;
}

void scr_refresh(struct scroller *scr, int focus, enum win_refresh dorefresh)
{
    int height;
    int width;
    vterminal_get_height_width(scr->vt, height, width);

    int vterm_cursor_row, vterm_cursor_col;
    vterminal_get_cursor_pos(scr->vt, vterm_cursor_row, vterm_cursor_col);

    int sb_num_rows;
    vterminal_get_sb_num_rows(scr->vt, sb_num_rows);

    int delta;
    vterminal_scroll_get_delta(scr->vt, delta);

    // TODO: Need to highlight searching if option is set
    int hlsearch = cgdbrc_get_int(CGDBRC_HLSEARCH);
    
    int highlight_attr, search_attr;

    int cursor_row, cursor_col;

    if (scr->in_scroll_mode) {
        cursor_row = scr->scroll_cursor_row;
        cursor_col = scr->scroll_cursor_col;
    } else {
        cursor_row = vterm_cursor_row;
        cursor_col = vterm_cursor_col;
    }

    /* Steal line highlight attribute for our scroll mode status */
    highlight_attr = hl_groups_get_attr(hl_groups_instance,
        HLG_SCROLL_MODE_STATUS);

    search_attr = hl_groups_get_attr(hl_groups_instance, HLG_INCSEARCH);

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            char *utf8buf;
            int attr = 0;

            int in_search = scr->in_search_mode && scr->search_row == r &&
                    c >= scr->search_col_start && c < scr->search_col_end;

            vterminal_fetch_row(scr->vt, r, c, c + 1, utf8buf, attr);
            swin_wmove(scr->win, r,  c);
            swin_wattron(scr->win, attr);
            if (in_search)
                swin_wattron(scr->win, search_attr);
            swin_waddnstr(scr->win, utf8buf, strlen(utf8buf));
            if (in_search)
                swin_wattroff(scr->win, search_attr);
            swin_wattroff(scr->win, attr);
            swin_wclrtoeol(scr->win);
        }

        // If in scroll mode, overlay the percent the scroller is scrolled
        // back on the top right of the scroller display.
        if (scr->in_scroll_mode && r == 0) {
            char status[ 64 ];
            size_t status_len;

            snprintf(status, sizeof(status), "[%d/%d]", delta, sb_num_rows);

            status_len = strlen(status);
            if ( status_len < width ) {
                swin_wattron(scr->win, highlight_attr);
                swin_mvwprintw(scr->win, r, width - status_len, "%s", status);
                swin_wattroff(scr->win, highlight_attr);
            }
        }
    }

    // Show the cursor when the scroller is in focus
    if (focus) {
        swin_wmove(scr->win, cursor_row, cursor_col);
        swin_curs_set(1);
    } else {
        /* Hide the cursor */
        swin_curs_set(0);
    }

    switch(dorefresh) {
        case WIN_NO_REFRESH:
            swin_wnoutrefresh(scr->win);
            break;
        case WIN_REFRESH:
            swin_wrefresh(scr->win);
            break;
    }
}
