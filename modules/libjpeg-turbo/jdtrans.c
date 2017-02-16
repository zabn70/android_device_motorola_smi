/*
 * jdtrans.c
 *
 * Copyright (C) 1995-1997, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains library routines for transcoding decompression,
 * that is, reading raw DCT coefficient arrays from an input JPEG file.
 * The routines in jdapimin.c will also be needed by a transcoder.
 */

#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#ifdef USE_INTEL_JPEGDEC
#include "jd_libva.h"
#endif

/* Forward declarations */
LOCAL(void) transdecode_master_selection JPP((j_decompress_ptr cinfo));


/*
 * Read the coefficient arrays from a JPEG file.
 * jpeg_read_header must be completed before calling this.
 *
 * The entire image is read into a set of virtual coefficient-block arrays,
 * one per component.  The return value is a pointer to the array of
 * virtual-array descriptors.  These can be manipulated directly via the
 * JPEG memory manager, or handed off to jpeg_write_coefficients().
 * To release the memory occupied by the virtual arrays, call
 * jpeg_finish_decompress() when done with the data.
 *
 * An alternative usage is to simply obtain access to the coefficient arrays
 * during a buffered-image-mode decompression operation.  This is allowed
 * after any jpeg_finish_output() call.  The arrays can be accessed until
 * jpeg_finish_decompress() is called.  (Note that any call to the library
 * may reposition the arrays, so don't rely on access_virt_barray() results
 * to stay valid across library calls.)
 *
 * Returns NULL if suspended.  This case need be checked only if
 * a suspending data source is used.
 */

GLOBAL(jvirt_barray_ptr *)
jpeg_read_coefficients (j_decompress_ptr cinfo)
{
  if (cinfo->global_state == DSTATE_READY) {
    /* First call: initialize active modules */
    transdecode_master_selection(cinfo);
    cinfo->global_state = DSTATE_RDCOEFS;
  }
  if (cinfo->global_state == DSTATE_RDCOEFS) {
    /* Absorb whole file into the coef buffer */
    for (;;) {
      int retcode;
      /* Call progress monitor hook if present */
      if (cinfo->progress != NULL)
	(*cinfo->progress->progress_monitor) ((j_common_ptr) cinfo);
      /* Absorb some more input */
      retcode = (*cinfo->inputctl->consume_input) (cinfo);
      if (retcode == JPEG_SUSPENDED)
	return NULL;
      if (retcode == JPEG_REACHED_EOI)
	break;
      /* Advance progress counter if appropriate */
      if (cinfo->progress != NULL &&
	  (retcode == JPEG_ROW_COMPLETED || retcode == JPEG_REACHED_SOS)) {
	if (++cinfo->progress->pass_counter >= cinfo->progress->pass_limit) {
	  /* startup underestimated number of scans; ratchet up one scan */
	  cinfo->progress->pass_limit += (long) cinfo->total_iMCU_rows;
	}
      }
    }
    /* Set state so that jpeg_finish_decompress does the right thing */
    cinfo->global_state = DSTATE_STOPPING;
  }
  /* At this point we should be in state DSTATE_STOPPING if being used
   * standalone, or in state DSTATE_BUFIMAGE if being invoked to get access
   * to the coefficients during a full buffered-image-mode decompression.
   */
  if ((cinfo->global_state == DSTATE_STOPPING ||
       cinfo->global_state == DSTATE_BUFIMAGE) && cinfo->buffered_image) {
    return cinfo->coef->coef_arrays;
  }
  /* Oops, improper usage */
  ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  return NULL;			/* keep compiler happy */
}

LOCAL(boolean)
jpeg_build_huffman_index_progressive(j_decompress_ptr cinfo,
        huffman_index *index)
{
  if (cinfo->global_state == DSTATE_READY) {
    printf("Progressive Mode\n");
    /* First call: initialize active modules */
    transdecode_master_selection(cinfo);
    cinfo->global_state = DSTATE_RDCOEFS;
  }
  if (cinfo->global_state == DSTATE_RDCOEFS) {
    int mcu, i;
    cinfo->marker->get_sos_marker_position(cinfo, index);

    /* Absorb whole file into the coef buffer */
    for (mcu = 0; mcu < cinfo->total_iMCU_rows; mcu++) {
      int retcode = 0;
      /* Call progress monitor hook if present */
      if (cinfo->progress != NULL)
        (*cinfo->progress->progress_monitor) ((j_common_ptr) cinfo);
      /* Absorb some more input */
      jinit_phuff_decoder(cinfo);
      for (i = 0; i < index->scan_count; i++) {
        (*cinfo->inputctl->finish_input_pass) (cinfo);
        jset_input_stream_position(cinfo, index->scan[i].bitstream_offset);
        cinfo->unread_marker = 0;
        retcode = (*cinfo->inputctl->consume_input_build_huffman_index)
                    (cinfo, index, i);
        if (retcode == JPEG_REACHED_EOI)
          break;
        cinfo->input_iMCU_row = mcu;
        if (mcu != 0)
          (*cinfo->entropy->configure_huffman_decoder)
                (cinfo, index->scan[i].prev_MCU_offset);
        cinfo->input_scan_number = i;
        retcode = (*cinfo->inputctl->consume_input_build_huffman_index)
                    (cinfo, index, i);
      }
      if (retcode == JPEG_SUSPENDED)
        return FALSE;
      if (retcode == JPEG_REACHED_EOI)
        break;
      /* Advance progress counter if appropriate */
      if (cinfo->progress != NULL &&
	  (retcode == JPEG_ROW_COMPLETED || retcode == JPEG_REACHED_SOS)) {
        if (++cinfo->progress->pass_counter >= cinfo->progress->pass_limit) {
	  /* startup underestimated number of scans; ratchet up one scan */
          cinfo->progress->pass_limit += (long) cinfo->total_iMCU_rows;
        }
      }
    }
    cinfo->global_state = DSTATE_STOPPING;
  }
  /* At this point we should be in state DSTATE_STOPPING if being used
   * standalone, or in state DSTATE_BUFIMAGE if being invoked to get access
   * to the coefficients during a full buffered-image-mode decompression.
   */
  if ((cinfo->global_state == DSTATE_STOPPING ||
       cinfo->global_state == DSTATE_BUFIMAGE) && cinfo->buffered_image) {
    return TRUE;
  }
  /* Oops, improper usage */
  ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  return FALSE;			/* keep compiler happy */
}

LOCAL(boolean)
jpeg_build_huffman_index_baseline(j_decompress_ptr cinfo, huffman_index *index)
{
  if (cinfo->global_state == DSTATE_READY) {
    printf("Baseline Mode\n");
    /* First call: initialize active modules */
    transdecode_master_selection(cinfo);
    cinfo->global_state = DSTATE_RDCOEFS;
  }
  if (cinfo->global_state == DSTATE_RDCOEFS) {
    /* Absorb whole file into the coef buffer */
    for (;;) {
      int retcode;
      /* Call progress monitor hook if present */
      if (cinfo->progress != NULL)
        (*cinfo->progress->progress_monitor) ((j_common_ptr) cinfo);
      /* Absorb some more input */
      retcode = (*cinfo->inputctl->consume_input_build_huffman_index)
                    (cinfo, index, 0);
      if (retcode == JPEG_SUSPENDED)
        return FALSE;
      if (retcode == JPEG_REACHED_EOI)
        break;
      if (retcode == JPEG_SCAN_COMPLETED)
        break;

      /* Advance progress counter if appropriate */
      if (cinfo->progress != NULL &&
	  (retcode == JPEG_ROW_COMPLETED || retcode == JPEG_REACHED_SOS)) {
        if (++cinfo->progress->pass_counter >= cinfo->progress->pass_limit) {
	  /* startup underestimated number of scans; ratchet up one scan */
        cinfo->progress->pass_limit += (long) cinfo->total_iMCU_rows;
        }
      }
    }
    /* Set state so that jpeg_finish_decompress does the right thing */
    cinfo->global_state = DSTATE_STOPPING;
  }
  /* At this point we should be in state DSTATE_STOPPING if being used
   * standalone, or in state DSTATE_BUFIMAGE if being invoked to get access
   * to the coefficients during a full buffered-image-mode decompression.
   */
  if ((cinfo->global_state == DSTATE_STOPPING ||
       cinfo->global_state == DSTATE_BUFIMAGE) && cinfo->buffered_image) {
    return TRUE;
  }
  /* Oops, improper usage */
  ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  return FALSE;			/* keep compiler happy */
}

GLOBAL(boolean)
jpeg_build_huffman_index_native(j_decompress_ptr cinfo, huffman_index *index)
{
    cinfo->tile_decode = TRUE;
    if (cinfo->progressive_mode)
      return jpeg_build_huffman_index_progressive(cinfo, index);
    else
      return jpeg_build_huffman_index_baseline(cinfo, index);
}
GLOBAL(boolean)
jpeg_build_huffman_index(j_decompress_ptr cinfo, huffman_index *index)
{
#ifndef USE_INTEL_JPEGDEC
    return jpeg_build_huffman_index_native(cinfo, index);
#else
    return jpeg_build_huffman_index_hw(cinfo, index);
#endif
}

/*
 * Master selection of decompression modules for transcoding.
 * This substitutes for jdmaster.c's initialization of the full decompressor.
 */

LOCAL(void)
transdecode_master_selection (j_decompress_ptr cinfo)
{
  /* This is effectively a buffered-image operation. */
  cinfo->buffered_image = TRUE;

#if JPEG_LIB_VERSION >= 80
  /* Compute output image dimensions and related values. */
  jpeg_core_output_dimensions(cinfo);
#endif

  /* Entropy decoding: either Huffman or arithmetic coding. */
  if (cinfo->arith_code) {
#ifdef D_ARITH_CODING_SUPPORTED
    jinit_arith_decoder(cinfo);
#else
    ERREXIT(cinfo, JERR_ARITH_NOTIMPL);
#endif
  } else {
    if (cinfo->progressive_mode) {
#ifdef D_PROGRESSIVE_SUPPORTED
      jinit_phuff_decoder(cinfo);
#else
      ERREXIT(cinfo, JERR_NOT_COMPILED);
#endif
    } else
      jinit_huff_decoder(cinfo);
  }

  /* Always get a full-image coefficient buffer. */
  jinit_d_coef_controller(cinfo, TRUE);

  /* We can now tell the memory manager to allocate virtual arrays. */
  (*cinfo->mem->realize_virt_arrays) ((j_common_ptr) cinfo);

  /* Initialize input side of decompressor to consume first scan. */
  (*cinfo->inputctl->start_input_pass) (cinfo);

  /* Initialize progress monitoring. */
  if (cinfo->progress != NULL) {
    int nscans;
    /* Estimate number of scans to set pass_limit. */
    if (cinfo->progressive_mode) {
      /* Arbitrarily estimate 2 interleaved DC scans + 3 AC scans/component. */
      nscans = 2 + 3 * cinfo->num_components;
    } else if (cinfo->inputctl->has_multiple_scans) {
      /* For a nonprogressive multiscan file, estimate 1 scan per component. */
      nscans = cinfo->num_components;
    } else {
      nscans = 1;
    }
    cinfo->progress->pass_counter = 0L;
    cinfo->progress->pass_limit = (long) cinfo->total_iMCU_rows * nscans;
    cinfo->progress->completed_passes = 0;
    cinfo->progress->total_passes = 1;
  }
}
