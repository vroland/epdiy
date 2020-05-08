/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "epd_driver.h"
#include "firacode.h"
#include "unicode.h"


#define ECHO_TEST_TXD  (GPIO_NUM_1)
#define ECHO_TEST_RXD  (GPIO_NUM_3)
#define ECHO_TEST_RTS  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS  (UART_PIN_NO_CHANGE)

#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)

#define BUF_SIZE (1024)
#define ESC_BUF_SIZE (128 * 4)
#define ESC_ARG_SIZE  16
#define COLUMNS 70
#define ROWS 20

void csihandle(void);
void tclearregion(int x1, int y1, int x2, int y2);

int min(int a, int b) {
  return a < b ? a : b;
}

int max(int a, int b) {
  return a > b ? a : b;
}

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

int log_to_uart(const char* fmt, va_list args) {
    char buffer[256];
    int result = vsprintf(buffer, fmt, args);
    uart_write_bytes(UART_NUM_1, buffer, strnlen(buffer, 256));
    return result;
}

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct {
	char buf[ESC_BUF_SIZE]; /* raw string */
	size_t len;            /* raw string length */
	char priv;
	int arg[ESC_ARG_SIZE];
	int narg;              /* nb of args */
	char mode[2];
} CSIEscape;

enum escape_state {
	ESC_START      = 1,
	ESC_CSI        = 2,
	ESC_STR        = 4,  /* OSC, PM, APC */
	ESC_ALTCHARSET = 8,
	ESC_STR_END    = 16, /* a final string was encountered */
	ESC_TEST       = 32, /* Enter in test mode */
	ESC_UTF8       = 64,
	ESC_DCS        =128,
};

// inspired by the st - the simple terminal, suckless.org

typedef struct {
  bool dirty: 1;
  uint8_t color: 4;
} CharMeta;

typedef struct {
  CharMeta chars[COLUMNS];
  bool dirty;
} LineMeta;

// A line is a sequence of code points.
typedef uint32_t Line[COLUMNS];

typedef struct {
    int x;
    int y;
} Cursor;

typedef struct {
    /// Number of rows.
    int row;
    /// Number of columns.
    int col;
    Line line[ROWS];

    Line old_line[ROWS];

    LineMeta meta[ROWS];
    Cursor cursor;

    /// the pixel position of the first column
    int pixel_start_x;
    /// the pixel position of the first row
    int pixel_start_y;

    /// escape state flags
	int esc;

    /// the current CSI escape sequence
    CSIEscape csiescseq;
} Term;


static uint8_t uart_str_buffer[BUF_SIZE];
static uint8_t* uart_buffer_end = uart_str_buffer;
static uint8_t* uart_buffer_start = uart_str_buffer;
static Term term;

uint32_t read_char() {
    int remaining = uart_buffer_end - uart_buffer_start;
    if (uart_buffer_start >= uart_buffer_end
            || utf8_len(*uart_buffer_start) > remaining) {

        memmove(uart_str_buffer, uart_buffer_start, remaining);
        uart_buffer_start = uart_str_buffer;
        uart_buffer_end = uart_buffer_start + remaining;
        int unfilled = uart_str_buffer + BUF_SIZE - uart_buffer_end;
        int len = uart_read_bytes(UART_NUM_1, uart_buffer_end, unfilled, 20 / portTICK_RATE_MS);
        uart_buffer_end += len;
        if (len < 0) {
            ESP_LOGE("terminal", "uart read error");
            return 0;
        } else if (len == 0) {
            return 0;
        }
        remaining = uart_buffer_end - uart_buffer_start;
    }

    int bytes = utf8_len(*uart_buffer_start);
    if (remaining < bytes) {
      return 0;
    }

    uint32_t cp = to_cp((char*)uart_buffer_start);
    uart_buffer_start += bytes;
    return cp;
}

int calculate_horizontal_advance(GFXfont* font, Line line, int col) {
  int total = 0;
  for (int i = 0; i < col; i++) {
    int cp = line[i];
    GFXglyph* glyph;
    // use space width for unfilled columns
    if (cp == 0) {
        cp = 32;
    }
    get_glyph(font, cp, &glyph);

    if (!glyph) {
      ESP_LOGW("terminal", "no glyph for %d", cp);
    }
    total += glyph->advance_x;
  }
  return total;
}

void
tsetdirt(int top, int bot)
{
	int i, j;

	LIMIT(top, 0, term.row-1);
	LIMIT(bot, 0, term.row-1);

	for (i = top; i <= bot; i++) {
		term.meta[i].dirty = true;
        for (j = 0; j < COLUMNS; j++) {
            term.meta[i].chars[j].dirty = true;
        }
    }
}

void
tscrollup(int orig, int n)
{
	int i;
	Line temp;

	LIMIT(n, 0, ROWS - 1 -orig+1);

	tclearregion(0, orig, term.col-1, orig+n-1);
	tsetdirt(orig+n, ROWS - 1);

    // FIXME: Copy metadata!
	for (i = orig; i <= ROWS - 1 -n; i++) {
        ESP_LOGI("terminal", "copy line %d to %d", i + n, i);
        memcpy(temp, term.line[i], sizeof(Line));
        memcpy(term.line[i], term.line[i+n], sizeof(Line));
        memcpy(term.line[i+n], temp, sizeof(Line));
	}

	//selscroll(orig, -n);
}

void
tscrolldown(int orig, int n)
{
	int i;
	Line temp;

	LIMIT(n, 0, ROWS - 1 -orig+1);

	tsetdirt(orig, ROWS - 1-n);
	tclearregion(0, ROWS - 1-n+1, term.col-1, ROWS - 1);

	for (i = ROWS - 1; i >= orig+n; i--) {
        memcpy(temp, term.line[i], sizeof(Line));
        memcpy(term.line[i], term.line[i-n], sizeof(Line));
        memcpy(term.line[i-n], temp, sizeof(Line));
	}

	//selscroll(orig, n);
}

void tmoveto(int x, int y) {
  term.cursor.x = min(max(x, 0), COLUMNS - 1);
  term.cursor.y = min(max(y, 0), ROWS - 1);
}

void tputc(uint32_t chr) {
  term.meta[term.cursor.y].dirty = true;
  term.meta[term.cursor.y].chars[term.cursor.x].dirty = true;
  term.line[term.cursor.y][term.cursor.x] = chr;
}

void tinit() {
  term.pixel_start_x = 50;
  term.pixel_start_y = 50;
  term.col = 0;
  term.row = 0;
  term.cursor.x = 0;
  term.cursor.y = 0;
  memset(term.meta, 0, sizeof(term.meta));
  memset(term.line, 0, sizeof(term.line));
  memset(term.old_line, 0, sizeof(term.old_line));
  term.esc = 0;
}

enum RenderOperation {
  NOP = 0,
  WRITE = 1,
  DELETE = 2,
  REPLACE = 3,
};

static uint8_t* render_fb_write = NULL;
static uint8_t* render_fb_delete = NULL;

void render() {
  memset(render_fb_write, 255, EPD_WIDTH / 2 * EPD_HEIGHT);
  memset(render_fb_delete, 255, EPD_WIDTH / 2 * EPD_HEIGHT);

  int write_min_line = 100000;
  int write_max_line = 0;

  int delete_min_line = 100000;
  int delete_max_line = 0;

  for (int y = 0; y < ROWS; y++) {
    if (!term.meta[y].dirty) {
      continue;
    }

    for (int x = 0; x < COLUMNS; x++) {
      CharMeta* cm = &term.meta[y].chars[x];

      uint32_t chr = term.line[y][x];
      uint32_t old_chr = term.old_line[y][x];

      // determine operation to perform
      enum RenderOperation operation = NOP;
      if (!cm->dirty) {
        operation = NOP;
      } else if (chr != old_chr && old_chr && chr) {
        operation = REPLACE;
      } else if (chr != old_chr && old_chr && !chr) {
        operation = DELETE;
      } else if (chr != old_chr && !old_chr && chr) {
        operation = WRITE;
      }

      if (operation == DELETE || operation == REPLACE) {
        int horizontal_advance = calculate_horizontal_advance((GFXfont *) &FiraCode, term.old_line[y], x);
        int px_x = term.pixel_start_x + horizontal_advance;
        int px_y = term.pixel_start_y + FiraCode.advance_y * y;
        char data[5];
        to_utf8(data, old_chr);

        writeln((GFXfont *) &FiraCode, data, &px_x, &px_y, render_fb_delete);
        delete_min_line = min(y, delete_min_line);
        delete_max_line = max(y, delete_max_line);
      }

      if (operation == WRITE || operation == REPLACE) {
        int horizontal_advance = calculate_horizontal_advance((GFXfont *) &FiraCode, term.line[y], x);
        int px_x = term.pixel_start_x + horizontal_advance;
        int px_y = term.pixel_start_y + FiraCode.advance_y * y;
        char data[5];
        to_utf8(data, chr);

        // FIXME: color currently ignored
        writeln((GFXfont *) &FiraCode, data, &px_x, &px_y, render_fb_write);
        write_min_line = min(y, write_min_line);
        write_max_line = max(y, write_max_line);
      }
      cm->dirty = false;
    }
    term.meta[y].dirty = false;
  }
  memcpy(term.old_line, term.line, ROWS * sizeof(Line));

  // delete buffer dirty
  if (delete_min_line <= delete_max_line) {
    int offset = term.pixel_start_y + (delete_min_line - 1) * FiraCode.advance_y;
    offset = min(EPD_HEIGHT, max(0, offset));
    int height = (delete_max_line - delete_min_line + 1) * FiraCode.advance_y;
    height = min(height, EPD_HEIGHT - offset);
    uint8_t* start_ptr = render_fb_delete + EPD_WIDTH / 2 * offset;
    Rect_t area = {
      .x = 0,
      .y = offset,
      .width = EPD_WIDTH,
      .height = height,
    };
    epd_poweron();
    epd_draw_image(area, start_ptr, WHITE_ON_WHITE);
    epd_poweroff();
  }

  ESP_LOGI("terminal", "min / max line write: %d, %d", write_min_line, write_max_line);

  // write buffer dirty
  if (write_min_line <= write_max_line) {
    int offset = term.pixel_start_y + (write_min_line - 1) * FiraCode.advance_y;
    offset = min(EPD_HEIGHT, max(0, offset));
    int height = (write_max_line - write_min_line + 1) * FiraCode.advance_y;
    height = min(height, EPD_HEIGHT - offset);
    uint8_t* start_ptr = render_fb_write + EPD_WIDTH / 2 * offset;
    Rect_t area = {
      .x = 0,
      .y = offset,
      .width = EPD_WIDTH,
      .height = height,
    };
    epd_poweron();
    epd_draw_image(area, start_ptr, BLACK_ON_WHITE);
    epd_poweroff();
  }
}

void tnewline(bool carr_return) {
	int y = term.cursor.y;

	if (y == ROWS - 1) {
		tscrollup(0, 1);
	} else {
		y++;
	}
	tmoveto(carr_return ? 0 : term.cursor.x, y);
}

void csireset(void) {
	memset(&term.csiescseq, 0, sizeof(term.csiescseq));
}

void tcontrolcode(uint32_t chr) {
  switch (chr) {
    // ignore bell
    case '\a':
      break;
    // TODO: tabstop movement
    case '\t':
      tputc(32);
      tmoveto(term.cursor.x + 1, term.cursor.y);
      return;
    case '\b':
      tmoveto(term.cursor.x - 1, term.cursor.y);
      tputc(0);
      return;
    case '\r':
      tmoveto(0, term.cursor.y);
      return;
    // next line
    case '\v':
    case '\f':
    case '\n':
      tnewline(true);
      return;
	case '\032': /* SUB */
	case '\030': /* CAN */
		csireset();
		break;
    case '\033':
      csireset();
      term.esc &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
      term.esc |= ESC_START;
      break;
    default:
      // break if C1 characters
      if (chr >= 0x7F && chr <= 0x9F) {
        break;
      } else {
        return;
      }
  }
  /* only CAN, SUB, \a and C1 chars interrupt a sequence */
  term.esc &= ~(ESC_STR_END|ESC_STR);
}

/*
 * returns 1 when the sequence is finished and it hasn't to read
 * more characters for this sequence, otherwise 0
 */
int
eschandle(uint32_t chr)
{
	switch (chr) {
	case '[':
		term.esc |= ESC_CSI;
		return 0;
	case '#':
		term.esc |= ESC_TEST;
		return 0;
	case '%':
		term.esc |= ESC_UTF8;
		return 0;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */
		//TODO: tstrsequence(ascii);
		return 0;
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		//term.charset = 2 + (chr - 'n');
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		//term.icharset = chr - '(';
		term.esc |= ESC_ALTCHARSET;
		return 0;
	case 'D': /* IND -- Linefeed */
		if (term.cursor.y == ROWS - 1) {
			tscrollup(0, 1);
		} else {
			tmoveto(term.cursor.x, term.cursor.y + 1);
		}
		break;
	case 'E': /* NEL -- Next line */
		tnewline(true); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		//term.tabs[term.cursor.x] = 1;
		break;
	case 'M': /* RI -- Reverse index */
		if (term.cursor.y == 0) {
			tscrolldown(0, 1);
		} else {
			tmoveto(term.cursor.x, term.cursor.y - 1);
		}
		break;
	case 'Z': /* DECID -- Identify Terminal */
		//ttywrite(vtiden, strlen(vtiden), 0);
		break;
	case 'c': /* RIS -- Reset to initial state */
		tinit();
		break;
	case '=': /* DECPAM -- Application keypad */
		//xsetmode(1, MODE_APPKEYPAD);
		break;
	case '>': /* DECPNM -- Normal keypad */
		//xsetmode(0, MODE_APPKEYPAD);
		break;
	case '7': /* DECSC -- Save Cursor */
		//tcursor(CURSOR_SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		//tcursor(CURSOR_LOAD);
		break;
	case '\\': /* ST -- String Terminator */
		//if (term.esc & ESC_STR_END)
		//	strhandle();
		break;
	default:
		fprintf(stderr, "erresc: unknown sequence ESC 0x%X '%d'\n",
			chr, chr);
		break;
	}
	return 1;
}

void csiparse(void) {
	char *p = term.csiescseq.buf, *np;
	long int v;

	term.csiescseq.narg = 0;
	if (*p == '?') {
		term.csiescseq.priv = 1;
		p++;
	}

	term.csiescseq.buf[term.csiescseq.len] = '\0';
	while (p < term.csiescseq.buf+term.csiescseq.len) {
		np = NULL;
		v = strtol(p, &np, 10);
		if (np == p)
			v = 0;
		if (v == LONG_MAX || v == LONG_MIN)
			v = -1;
		term.csiescseq.arg[term.csiescseq.narg++] = v;
		p = np;
		if (*p != ';' || term.csiescseq.narg == ESC_ARG_SIZE)
			break;
		p++;
	}
	term.csiescseq.mode[0] = *p++;
	term.csiescseq.mode[1] = (p < term.csiescseq.buf+term.csiescseq.len) ? *p : '\0';
}

void epd_task() {
    epd_init();
    delay(300);
    epd_poweron();
    epd_clear();
    epd_poweroff();

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
    uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);

    // Still log to the serial output
    esp_log_set_vprintf(log_to_uart);

    ESP_LOGI("terminal", "terminal struct size: %u", sizeof(Term));
    render_fb_write = heap_caps_malloc(EPD_WIDTH / 2 * EPD_HEIGHT, MALLOC_CAP_SPIRAM);
    render_fb_delete = heap_caps_malloc(EPD_WIDTH / 2 * EPD_HEIGHT, MALLOC_CAP_SPIRAM);

    delay(1000);

    uart_write_bytes(UART_NUM_1, "listening\n", 11);

    tinit();

    while (true) {

        uint32_t chr = read_char();
        if (chr == 0) {
          render();
          continue;
        };

        if (chr < 32 || (chr >= 0x7F && chr <= 0x9F)) {
          tcontrolcode(chr);
        } else if (term.esc & ESC_START) {
          if (term.esc & ESC_CSI) {
              term.csiescseq.buf[term.csiescseq.len++] = chr;
              if (BETWEEN(chr, 0x40, 0x7E)
                      || term.csiescseq.len >= \
                      sizeof(term.csiescseq.buf)-1) {
                  term.esc = 0;
                  csiparse();
                  csihandle();
              }
              continue;
          } else if (term.esc & ESC_UTF8) {
              //tdefutf8(u);
          } else if (term.esc & ESC_ALTCHARSET) {
              //tdeftran(u);
          } else if (term.esc & ESC_TEST) {
              //tdectest(u);
          } else {
              if (!eschandle(chr))
                  continue;
              /* sequence already finished */
          }
          term.esc = 0;
          /*
           * All characters which form part of a sequence are not
           * printed
           */
        } else {
          tputc(chr);
          tmoveto(term.cursor.x + 1, term.cursor.y);
        }
    }
}

void app_main() {
  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}

void
tclearregion(int x1, int y1, int x2, int y2)
{
	int x, y, temp;

	if (x1 > x2)
		temp = x1, x1 = x2, x2 = temp;
	if (y1 > y2)
		temp = y1, y1 = y2, y2 = temp;

	LIMIT(x1, 0, term.col-1);
	LIMIT(x2, 0, term.col-1);
	LIMIT(y1, 0, term.row-1);
	LIMIT(y2, 0, term.row-1);

	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
            int backup_x = term.cursor.x;
            int backup_y = term.cursor.y;

            tmoveto(x, y);
            tputc(0);
            tmoveto(backup_x, backup_y);
		}
	}
    ESP_LOGI("terminal", "clearing region %d,%d,%d,%d", x1, y1, x2, y2);
}



void csihandle(void) {
	char buf[40];
	int len;

    ESP_LOGI("terminal", "csi type: %d", term.csiescseq.mode[0]);

	switch (term.csiescseq.mode[0]) {
	default:
	unknown:
		fprintf(stderr, "erresc: unknown csi ");
		//csidump();
		/* die(""); */
		break;
	case '@': /* ICH -- Insert <n> blank char */
		DEFAULT(term.csiescseq.arg[0], 1);
		//TODO: tinsertblank(term.csiescseq.arg[0]);
		break;
	case 'A': /* CUU -- Cursor <n> Up */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(term.cursor.x, term.cursor.y-term.csiescseq.arg[0]);
		break;
	case 'B': /* CUD -- Cursor <n> Down */
	case 'e': /* VPR --Cursor <n> Down */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(term.cursor.x, term.cursor.y + term.csiescseq.arg[0]);
		break;
    /*
	case 'i': // MC -- Media Copy
		switch (csiescseq.arg[0]) {
		case 0:
			tdump();
			break;
		case 1:
			tdumpline(term.cursor.y);
			break;
		case 2:
			tdumpsel();
			break;
		case 4:
			term.mode &= ~MODE_PRINT;
			break;
		case 5:
			term.mode |= MODE_PRINT;
			break;
		}
		break;
	case 'c': // DA -- Device Attributes
		if (csiescseq.arg[0] == 0)
			ttywrite(vtiden, strlen(vtiden), 0);
		break;
    */
	case 'C': // CUF -- Cursor <n> Forward
	case 'a': /* HPR -- Cursor <n> Forward */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(term.cursor.x + term.csiescseq.arg[0], term.cursor.y);
		break;
	case 'D': /* CUB -- Cursor <n> Backward */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(term.cursor.x - term.csiescseq.arg[0], term.cursor.y);
		break;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(0, term.cursor.y+term.csiescseq.arg[0]);
		break;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(0, term.cursor.y-term.csiescseq.arg[0]);
		break;
    /*
	case 'g': // TBC -- Tabulation clear
		switch (csiescseq.arg[0]) {
		case 0: // clear current tab stop
			term.tabs[term.cursor.x] = 0;
			break;
		case 3: // clear all the tabs
			memset(term.tabs, 0, term.col * sizeof(*term.tabs));
			break;
		default:
			goto unknown;
		}
		break;
    */
	case 'G': /* CHA -- Move to <col> */
	case '`': /* HPA */
		DEFAULT(term.csiescseq.arg[0], 1);
		tmoveto(term.csiescseq.arg[0]-1, term.cursor.y);
		break;
	case 'H': /* CUP -- Move to <row> <col> */
	case 'f': /* HVP */
		DEFAULT(term.csiescseq.arg[0], 1);
		DEFAULT(term.csiescseq.arg[1], 1);
		tmoveto(term.csiescseq.arg[1]-1, term.csiescseq.arg[0]-1);
		break;
    /*
	case 'I': // CHT -- Cursor Forward Tabulation <n> tab stops
		DEFAULT(term.csiescseq.arg[0], 1);
		tputtab(term.csiescseq.arg[0]);
		break;
    */
	case 'J': // ED -- Clear screen
		switch (term.csiescseq.arg[0]) {
		case 0: /* below */
			tclearregion(term.cursor.x, term.cursor.y, COLUMNS-1, term.cursor.y);
			if (term.cursor.y < term.row-1) {
				tclearregion(0, term.cursor.y+1, term.col-1,
						term.row-1);
			}
			break;
		case 1: /* above */
			if (term.cursor.y > 1)
				tclearregion(0, 0, term.col-1, term.cursor.y-1);
			tclearregion(0, term.cursor.y, term.cursor.x, term.cursor.y);
			break;
		case 2: /* all */
			tclearregion(0, 0, term.col-1, term.row-1);
			break;
		default:
			goto unknown;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (term.csiescseq.arg[0]) {
		case 0: /* right */
			tclearregion(term.cursor.x, term.cursor.y, term.col-1,
					term.cursor.y);
			break;
		case 1: /* left */
			tclearregion(0, term.cursor.y, term.cursor.x, term.cursor.y);
			break;
		case 2: /* all */
			tclearregion(0, term.cursor.y, term.col-1, term.cursor.y);
			break;
		}
		break;
    }
}
