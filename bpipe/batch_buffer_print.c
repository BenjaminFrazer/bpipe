#include <stdio.h>
#include <string.h>
#include "batch_buffer.h"

/* ANSI color codes - disabled for now */
#define ANSI_COLOR_RED ""
#define ANSI_COLOR_GREEN ""
#define ANSI_COLOR_YELLOW ""
#define ANSI_COLOR_BLUE ""
#define ANSI_COLOR_MAGENTA ""
#define ANSI_COLOR_CYAN ""
#define ANSI_COLOR_RESET ""
#define ANSI_BOLD ""

/* Maximum batches to display before truncating */
#define MAX_DISPLAY_BATCHES 20

static const char* dtype_to_string(SampleDtype_t dtype)
{
  switch (dtype) {
    case DTYPE_FLOAT:
      return "FLOAT";
    case DTYPE_I32:
      return "I32";
    case DTYPE_U32:
      return "U32";
    case DTYPE_NDEF:
      return "UNDEFINED";
    default:
      return "UNKNOWN";
  }
}

static const char* overflow_to_string(OverflowBehaviour_t behaviour)
{
  switch (behaviour) {
    case OVERFLOW_BLOCK:
      return "BLOCK";
    case OVERFLOW_DROP_HEAD:
      return "DROP_HEAD";
    case OVERFLOW_DROP_TAIL:
      return "DROP_TAIL";
    default:
      return "UNKNOWN";
  }
}

/* Format timestamp for display (converts nanoseconds to readable format) */
static void format_timestamp(long long ns, char* buf, size_t buf_size)
{
  if (ns == 0) {
    snprintf(buf, buf_size, "---");
    return;
  }

  long long us = ns / 1000;
  long long ms = us / 1000;
  long long s = ms / 1000;

  if (s > 0) {
    snprintf(buf, buf_size, "%lld.%03llds", s, ms % 1000);
  } else if (ms > 0) {
    snprintf(buf, buf_size, "%lld.%03lldms", ms, us % 1000);
  } else if (us > 0) {
    snprintf(buf, buf_size, "%lldμs", us);
  } else {
    snprintf(buf, buf_size, "%lldns", ns);
  }
}

/* Print a single batch representation */
static void print_batch(size_t idx, Batch_t* batch, bool is_head, bool is_tail,
                        bool has_data)
{
  char ts_buf[32];
  char markers[8] = "     "; /* 5 spaces for markers */

  /* Set markers */
  if (is_head && is_tail) {
    strcpy(markers, " H=T ");
  } else if (is_head) {
    strcpy(markers, "  H  ");
  } else if (is_tail) {
    strcpy(markers, "  T  ");
  }

  if (!has_data) {
    printf("│  [%3zu] %-30s%s                                   │", /* 76->80:
                                                                       added 4
                                                                       spaces */
           idx, "", markers);
    return;
  }

  /* Format data for filled slots */
  format_timestamp(batch->t_ns, ts_buf, sizeof(ts_buf));

  /* Build data string */
  char data_str[64]; /* Larger buffer for formatting */
  snprintf(data_str, sizeof(data_str), "ID:%5zu %s", batch->batch_id, ts_buf);

  /* Ensure it fits in 30 chars */
  if (strlen(data_str) > 30) {
    data_str[30] = '\0';
  }

  printf(
      "│  [%3zu] %-30s%s                                   │", /* 76->80: added
                                                                  4 spaces */
      idx, data_str, markers);
}

/* Pretty print the batch buffer */
void bb_print(Batch_buff_t* buff)
{
  if (!buff) {
    printf("NULL buffer\n");
    return;
  }

  size_t head_idx = bb_get_head_idx(buff);
  size_t tail_idx = bb_get_tail_idx(buff);
  size_t n_batches = bb_n_batches(buff);
  size_t batch_size = bb_batch_size(buff);

  /* Get current state */
  size_t head =
      atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
  size_t tail =
      atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
  size_t used = head - tail;
  bool is_empty = (head == tail);
  bool is_full = ((head + 1) & bb_modulo_mask(buff)) == tail;

  /* Header - 80 chars wide including borders */
  printf("\n");
  printf(
      "╔═══════════════════════════════════════════════════════════════════════"
      "═══════╗\n");
  printf("║ Batch Buffer: %-62s ║\n", buff->name); /* 79->80: added space */
  printf(
      "╠═══════════════════════════════════════════════════════════════════════"
      "═══════╣\n");

  /* Buffer info - each line must be exactly 80 chars including borders */
  printf(
      "║ Type: %-8s │ Batches: %4zu │ Batch Size: %4zu │ Overflow: %-10s     "
      "║\n", /* 75->80: added 5 spaces */
      dtype_to_string(buff->dtype), n_batches, batch_size,
      overflow_to_string(buff->overflow_behaviour));

  /* Status line */
  const char* status_text = is_empty ? "EMPTY" : (is_full ? "FULL" : "ACTIVE");
  printf(
      "║ Head: %4zu      │ Tail: %4zu    │ Used: %4zu/%4zu │ Status: %-12s     "
      "║\n", /* 75->80: added 5 spaces */
      head, tail, used, n_batches - 1, status_text);

  /* Statistics */
  uint64_t total = atomic_load(&buff->producer.total_batches);
  uint64_t dropped = atomic_load(&buff->producer.dropped_batches);
  printf(
      "║ Total: %-8llu │ Dropped: %-8llu │ Drop Rate: %5.1f%%                  "
      "    ║\n", /* 79->80: added 1 more space */
      (unsigned long long) total, (unsigned long long) dropped,
      total > 0 ? (100.0 * dropped / total) : 0.0);

  printf(
      "╠═══════════════════════════════════════════════════════════════════════"
      "═══════╣\n");

  /* Legend */
  printf(
      "║ H=Head(Write) T=Tail(Read) │ [idx] ID:batch_id timestamp              "
      "       ║\n"); /* 79->80: added 1 space */
  printf(
      "╠═══════════════════════════════════════════════════════════════════════"
      "═══════╣\n");

  /* Determine display range */
  size_t display_start = 0;
  size_t display_end = n_batches;
  bool truncated = false;

  if (n_batches > MAX_DISPLAY_BATCHES) {
    /* Show first 8, ellipsis, last 8, with head/tail area prioritized */
    truncated = true;

    /* Try to include both head and tail areas */
    if (head_idx < 8 || tail_idx < 8) {
      display_start = 0;
      display_end = 8;
    } else if (head_idx >= n_batches - 8 || tail_idx >= n_batches - 8) {
      display_start = n_batches - 8;
      display_end = n_batches;
    } else {
      /* Show area around tail */
      display_start = (tail_idx > 4) ? tail_idx - 4 : 0;
      display_end = display_start + 8;
      if (display_end > n_batches) {
        display_end = n_batches;
        display_start = n_batches - 8;
      }
    }
  }

  /* Print batches in display range */
  for (size_t i = display_start; i < display_end; i++) {
    bool is_head = (i == head_idx);
    bool is_tail = (i == tail_idx);

    /* Check if this slot has valid data */
    bool has_data = false;
    if (!is_empty) {
      if (head > tail) {
        has_data = (i >= tail_idx && i < head_idx);
      } else {
        has_data = (i >= tail_idx || i < head_idx);
      }
    }

    print_batch(i, &buff->batch_ring[i], is_head, is_tail, has_data);
    printf("\n");
  }

  /* Show ellipsis if truncated */
  if (truncated && display_end < n_batches - 8) {
    printf(
        "║                           ...                                       "
        "         ║\n");

    /* Show last few batches */
    for (size_t i = n_batches - 8; i < n_batches; i++) {
      bool is_head = (i == head_idx);
      bool is_tail = (i == tail_idx);

      bool has_data = false;
      if (!is_empty) {
        if (head > tail) {
          has_data = (i >= tail_idx && i < head_idx);
        } else {
          has_data = (i >= tail_idx || i < head_idx);
        }
      }

      print_batch(i, &buff->batch_ring[i], is_head, is_tail, has_data);
      printf("\n");
    }
  }

  printf(
      "╚═══════════════════════════════════════════════════════════════════════"
      "═══════╝\n\n");
}

/* Print buffer summary (compact version) */
void bb_print_summary(Batch_buff_t* buff)
{
  if (!buff) {
    printf("NULL buffer\n");
    return;
  }

  size_t head =
      atomic_load_explicit(&buff->producer.head, memory_order_relaxed);
  size_t tail =
      atomic_load_explicit(&buff->consumer.tail, memory_order_relaxed);
  size_t used = head - tail;
  size_t capacity = bb_n_batches(buff) - 1;

  printf("[%-20s] %s %3zu/%3zu (%5.1f%%) H:%4zu T:%4zu\n", buff->name,
         dtype_to_string(buff->dtype), used, capacity,
         capacity > 0 ? (100.0 * used / capacity) : 0.0, head, tail);
}