/* Adaptive threshold edge detection plug-in for GIMP
 * Copyright (C) 2011 Dan Parnham <dan.parnham@gmail.com>
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <string.h>


/*******************************************************/
/*               Function Prototypes                   */
/*******************************************************/
static void query(void);
static void run(const gchar *name, gint nparams, const GimpParam *param, gint *nreturn_vals, GimpParam **return_vals);
static void edge_preview(GimpPreview *preview);
static void edge(GimpDrawable *drawable, GimpPreview *preview);

static gboolean edge_dialog(GimpDrawable *drawable);

static gint get_threshold(guchar *in, gint size);
static void greyscale(guchar *src, guchar *dst, gint size, gint channels, gboolean alpha);
static void blur(guchar *src, guchar *dst, gint width, gint height);
static void downsize(guchar *src, guchar *dst, gint w, gint h, gint nw, gint nh);
static void magnitude(guchar *in, gint *out, gint w, gint h);
static void upsizei(gint *src, gint *dst, gint w, gint h, gint nw, gint nh);
static void multiply(guchar *a, gint *b, gint *out, gint size);
static void bluri(gint *src, gint *dst, gint width, gint height);
static void divide(gint *a, gint *b, guchar *out, gint size);
static void upsize(guchar *src, guchar *dst, gint w, gint h, gint nw, gint nh);
static void apply_threshold(guchar *blurred, gint *mag, guchar *buffer, gint size, gint threshold);
static void filter(guchar *blurred, guchar *out, gint width, gint height, gint channels, gboolean alpha);


/*******************************************************/
/*                  Local Variables                    */
/*******************************************************/
GimpPlugInInfo PLUG_IN_INFO = {
	NULL,
	NULL,
	query,
	run
};


typedef struct
{
	gboolean automatic;
	gint     threshold;
} EdgeValues;


static EdgeValues evals = { 1, 16 };



/*******************************************************/
/*                 Plug-in Functions                   */
/*******************************************************/
MAIN()


static void query (void)
{
	static GimpParamDef args[] = {
		{ GIMP_PDB_INT32,		"run-mode",	"Interactive, non-interactive"								},
		{ GIMP_PDB_IMAGE,		"image",	"Image"														},
		{ GIMP_PDB_DRAWABLE,	"drawable",	"Drawable"													},
		{ GIMP_PDB_INT8,		"automatic", "Automatic thresholding (threshold value will be ignored)" },
		{ GIMP_PDB_INT32,		"threshold", "Edge detection threshold"									}
	};

	gimp_install_procedure (
		"plug-in-adaptive-edge",
		"Adaptive threshold edge detect",
		"Perform edge detection using an adaptive thresholding algorithm",
		"Daniel Parnham",
		"Copyright Daniel Parnham",
		"2011",
		"Adaptive Edge Detect...",
		"RGB*, GRAY*",
		GIMP_PLUGIN,
		G_N_ELEMENTS(args), 0,
		args, NULL
	);

	gimp_plugin_menu_register ("plug-in-adaptive-edge", "<Image>/Filters/Edge-Detect");
}


static void run(const gchar *name, gint nparams, const GimpParam *param, gint *nreturn_vals, GimpParam **return_vals)
{
	static GimpParam values[1];

	*nreturn_vals 			= 1;
	*return_vals			= values;
	values->type 			= GIMP_PDB_STATUS;
	values->data.d_status	= GIMP_PDB_SUCCESS;
	GimpRunMode mode		= param[0].data.d_int32;
	GimpDrawable *drawable	= gimp_drawable_get(param[2].data.d_drawable);

	if (gimp_drawable_is_rgb(drawable->drawable_id) || gimp_drawable_is_gray(drawable->drawable_id))
    {
		gimp_tile_cache_ntiles (48);

		switch (mode)
		{
			case GIMP_RUN_INTERACTIVE:		gimp_get_data("plug-in-adaptive-edge", &evals);
											if (!edge_dialog(drawable))
											{
												gimp_drawable_detach(drawable);
												return;
											}
											break;

			case GIMP_RUN_NONINTERACTIVE:	if (nparams == 5)
											{
												evals.automatic = param[3].data.d_int8 > 0;
												evals.threshold = param[4].data.d_int32;
											}
											else
											{
												values->data.d_status = GIMP_PDB_CALLING_ERROR;
												gimp_drawable_detach(drawable);
												return;
											}
											break;

			case GIMP_RUN_WITH_LAST_VALS:	gimp_get_data("plug-in-adaptive-edge", &evals);
											break;
		}

		edge(drawable, NULL);

		gimp_displays_flush();

		if (mode == GIMP_RUN_INTERACTIVE) gimp_set_data ("plug-in-adaptive-edge", &evals, sizeof(EdgeValues));
	}
	else values->data.d_status = GIMP_PDB_EXECUTION_ERROR;

	gimp_drawable_detach(drawable);
}


/*******************************************************/
/*                 Edge Detection                      */
/*******************************************************/
static void edge_preview(GimpPreview *preview)
{
	edge(gimp_drawable_preview_get_drawable(GIMP_DRAWABLE_PREVIEW(preview)), preview);
}


static void edge(GimpDrawable *drawable, GimpPreview *preview)
{
	GimpPixelRgn rin, rout;
	gint x1, y1, x2, y2, width, height;
	gint channels	= gimp_drawable_bpp(drawable->drawable_id);
	gboolean alpha	= gimp_drawable_has_alpha(drawable->drawable_id);

	if (preview)
	{
		if (evals.automatic) return;

		gimp_preview_get_position(preview, &x1, &y1);
		gimp_preview_get_size(preview, &width, &height);

		x2 = x1 + width;
		y2 = y1 + height;
	}
	else
	{
		gimp_progress_init("Adaptive Edge Detect...");
		gimp_drawable_mask_bounds(drawable->drawable_id, &x1, &y1, &x2, &y2);

		width	= x2 - x1;
		height	= y2 - y1;
	}

	gimp_pixel_rgn_init(&rin, drawable, x1, y1, width, height, FALSE, FALSE);
	gimp_pixel_rgn_init(&rout, drawable, x1, y1, width, height, TRUE, TRUE);

	gint size	= width * height;
	guchar *in	= g_new(guchar, size * channels);
	guchar *out = g_new(guchar, size * channels);

	gimp_pixel_rgn_get_rect(&rin, in, x1, y1, width, height);

	if (alpha) channels--;
	gint halfWidth		= width / 2;
	gint halfHeight		= height / 2;
	gint halfSize		= halfWidth * halfHeight;
	guchar *blurred		= g_new(guchar, size);
	guchar *blurredHalf	= g_new(guchar, halfSize);
	guchar *buffer		= g_new(guchar, size);
	gint *threshHalf	= g_new(gint, halfSize);
	gint *mag			= g_new(gint, size);
	gint *magHalf		= g_new(gint, halfSize);
	gint *filtThresh	= g_new(gint, halfSize);
	gint *filtMag		= g_new(gint, halfSize);

	// We only need to work in greyscale
	greyscale(in, buffer, size, channels, alpha);

	// Either automatically calculate a threshold or used the user-supplied one
	gint threshold = evals.automatic ? get_threshold(buffer, size) : evals.threshold;

	// Initial smoothing
	blur(buffer, blurred, width, height);
	downsize(blurred, blurredHalf, width, height, halfWidth, halfHeight);

	// Calculate edge magnitude from the downsized blurred image
	magnitude(blurredHalf, magHalf, halfWidth, halfHeight);
	upsizei(magHalf, mag, halfWidth, halfHeight, width, height);

	// Multiply the blurred image with magnitude image
	multiply(blurredHalf, magHalf, threshHalf, halfSize);

	// Blur the new threshold image and the magnitude image
	bluri(threshHalf, filtThresh, halfWidth, halfHeight);
	bluri(magHalf, filtMag, halfWidth, halfHeight);

	// Divide the threshold image by the blurred magnitude image
	// then upscale (using the original downsized blurred image as a buffer)
	divide(filtThresh, filtMag, blurredHalf, halfSize);
	upsize(blurredHalf, buffer, halfWidth, halfHeight, width, height);

	// Threshold the image based on the magnitude and the threshold image
	apply_threshold(blurred, mag, buffer, size, threshold);

	//copy(blurred, out, size, channels, alpha);

	// Filter to find connected edges and remove lone edge points
	filter(blurred, out, width, height, channels, alpha);

	gimp_pixel_rgn_set_rect(&rout, out, x1, y1, width, height);

	g_free(filtMag);
	g_free(filtThresh);
	g_free(threshHalf);
	g_free(buffer);
	g_free(magHalf);
	g_free(mag);
	g_free(blurredHalf);
	g_free(blurred);
	g_free(out);
	g_free(in);

	if (preview)
	{
		gimp_drawable_preview_draw_region(GIMP_DRAWABLE_PREVIEW(preview), &rout);
	}
	else
	{
		gimp_drawable_flush(drawable);
		gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
		gimp_drawable_update(drawable->drawable_id, x1, y1, width, height);
	}
}


static gint get_threshold(guchar *in, gint size)
{
	gint i;
	double meanSum = 0, sum = 0, v;


	for (i=0; i<size; i++)
	{
		v = (double)*in++;
		meanSum += v;
		sum		+= v * v;
	}

	double mean		= meanSum / (double)size;
	double variance = (sum / (double)size) - (mean * mean);

	return (gint)lrint(0.5 * sqrt(variance));
}


static void greyscale(guchar *src, guchar *dst, gint size, gint channels, gboolean alpha)
{
	gint i, j, v;

	for (i=0; i<size; i++)
	{
		v = 0;
		for (j=0; j<channels; j++) v += *src++;
		if (alpha) src++;
		*dst++ = (guchar)(v / channels);
	}
}


static void blur(guchar *src, guchar *dst, gint width, gint height)
{
	gint x, y, w = width - 1, h = height - 1;

	guchar *ps = src + width;
	guchar *po = dst + width;
	guchar *pa = ps - 1;
	guchar *pb = ps + 1;
	guchar *pc = ps - width;
	guchar *pd = ps + width;
	guchar *pe = ps - width - 1;
	guchar *pf = ps - width + 1;
	guchar *pg = ps + width - 1;
	guchar *ph = ps + width + 1;

	g_memmove(dst, src, width);
	for (y=1; y<h; y++)
	{
		pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++;
		*po++ = *ps++;

		for (x=1; x<w; x++) *po++ = (*pa++ + *pb++ + *pc++ + *pd++ + *pe++ + *pf++ + *pg++ + *ph++ + *ps++) / 9;

		*po++ = *ps++;
		pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++;
	}
	g_memmove(po, ps, width);
}


static void downsize(guchar *src, guchar *dst, gint w, gint h, gint nw, gint nh)
{
	gint x, y;
	gboolean oddWidth	= (w % 2) == 1;
	gint jump			= oddWidth ? w + 1 : w;
	guchar *ps			= src;
	guchar *po			= dst;

	for (y=0; y<nh; y++)
	{
		for (x=0; x<nw; x++)
		{
			*po++ = *ps;
			ps += 2;
		}
		ps += jump;
	}
}


static void magnitude(guchar *in, gint *out, gint w, gint h)
{
	guchar *pa = in + w + 1;
	guchar *pb = in + w - 1;
	guchar *pc = in + 2 * w;
	guchar *pd = in;

	gint hor, ver;
	gint *pm = out;
	gint x, y;

	memset(pm, 0, w * sizeof(gint));
	pm += w;

	for (y=1; y<h-1; y++)
	{
		pa++; pb++; pc++; pd++; *pm++ = 0;

		for (x=1; x<w-1; x++)
		{
			ver = abs(*pa++ - *pb++);
			hor = abs(*pc++ - *pd++);

			if (ver > hor) *pm++ = ver + hor / 3;
			else		   *pm++ = hor + ver / 3;
		}

		pa++; pb++; pc++; pd++; *pm++ = 0;
	}
	memset(pm, 0, w * sizeof(gint));
}


static void upsizei(gint *src, gint *dst, gint w, gint h, gint nw, gint nh)
{
	gboolean oddWidth	= (nw % 2) == 1;
	gboolean oddHeight	= (nh % 2) == 1;

	gint prev, curr = 0;
	gint x, y;

	gint *ps = src;
	gint *po = dst;
	gint *pt = dst;
	gint *pb = dst + (nw << 1);

	for (y=0; y<h; y++)
	{
		prev	= *ps++;
		*po++	= prev;
		for (x=0; x<w-1; x++)
		{
			curr	= *ps++;
			*po++ 	= (prev + curr) >> 1;
			*po++ 	= curr;
			prev	= curr;
		}
		if (oddWidth) *po++  = (prev + curr) >> 1;
		*po++  = curr;
		po	  += nw;
	}

	if (oddHeight)
	{
		ps = po - (nw << 1);
		for (x=0; x<nw; x++) *po++ = *ps++;
	}

	po = dst + nw;

	for (y=1; y<nh-1; y+=2)
	{
		for (x=0; x<nw; x++) *po++ = (*pt++ + *pb++ ) >> 1;

		po += nw;
		pt += nw;
		pb += nw;
	}
	if (!oddHeight)
	{
		// In the case of an even height, the final row needs copying from the row above
		for (x=0; x<nw; x++) *po++ = *pt++;
	}
}


static void multiply(guchar *a, gint *b, gint *out, gint size)
{
	gint i;
	for (i=0; i<size; i++) *out++ = (gint)*a++ * *b++;
}


static void bluri(gint *src, gint *dst, gint width, gint height)
{
	gint x, y, w = width - 1, h = height - 1;

	gint *ps = src + width;
	gint *po = dst + width;
	gint *pa = ps - 1;
	gint *pb = ps + 1;
	gint *pc = ps - width;
	gint *pd = ps + width;
	gint *pe = ps - width - 1;
	gint *pf = ps - width + 1;
	gint *pg = ps + width - 1;
	gint *ph = ps + width + 1;

	g_memmove(dst, src, width * sizeof(gint));
	for (y=1; y<h; y++)
	{
		pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++;
		*po++ = *ps++;

		for (x=1; x<w; x++) *po++ = (*pa++ + *pb++ + *pc++ + *pd++ + *pe++ + *pf++ + *pg++ + *ph++ + *ps++) / 9;

		*po++ = *ps++;
		pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++;
	}
	g_memmove(po, ps, width * sizeof(gint));
}


static void divide(gint *a, gint *b, guchar *out, gint size)
{
	gint i, div;
	for (i=0; i<size; i++)
	{
		div 	= *b++;
		div 	= *a++ / (!div ? 1 : div);
		*out++	= (guchar)((div > 255) ? 255 : div);
	}
}


static void upsize(guchar *src, guchar *dst, gint w, gint h, gint nw, gint nh)
{
	gboolean oddWidth	= (nw % 2) == 1;
	gboolean oddHeight	= (nh % 2) == 1;

	gint x, y, prev, curr = 0;

	guchar *ps = src;
	guchar *po = dst;
	guchar *pt = dst;
	guchar *pb = dst + (nw << 1);

	for (y=0; y<h; y++)
	{
		prev	= *ps++;
		*po++	= (guchar)prev;
		for (x=0; x<w-1; x++)
		{
			curr	= *ps++;
			*po++ 	= (guchar)((prev + curr) >> 1);
			*po++ 	= (guchar)curr;
			prev	= curr;
		}
		if (oddWidth) *po++ = (guchar)((prev + curr) >> 1);
		*po++  = (guchar)curr;
		po	  += nw;
	}

	if (oddHeight)
	{
		ps = po - (nw << 1);
		for (x=0; x<nw; x++) *po++ = *ps++;
	}

	po = dst + nw;

	for (y=1; y<nh-1; y+=2)
	{
		for (x=0; x<nw; x++) *po++ = (guchar)(((gint)*pt++ + (gint)*pb++ ) >> 1);

		po += nw;
		pt += nw;
		pb += nw;
	}
	if (!oddHeight)
	{
		// In the case of an even height, the final row needs copying from the row above
		for (x=0; x<nw; x++) *po++ = *pt++;
	}
}


static void apply_threshold(guchar *blurred, gint *mag, guchar *buffer, gint size, gint threshold)
{
	gint i;
	for (i=0; i<size; i++)
	{
		if (*mag++ > threshold)	*blurred = (*blurred <= *buffer) ? 0 : 255;
		else					*blurred = 128;

		blurred++; buffer++;
	}
}


static void filter(guchar *blurred, guchar *out, gint width, gint height, gint channels, gboolean alpha)
{
	guchar v;
	gint x, y, i;
	gint w 		= width - 1;
	gint h 		= height - 1;
	gint bpp	= channels + (alpha ? 1 : 0);
	guchar *po	= out;
	guchar *pe	= blurred + width;
	guchar *pa	= pe - width - 1;
	guchar *pb	= pa + 1;
	guchar *pc	= pb + 1;
	guchar *pd	= pe - 1;
	guchar *pf	= pe + 1;
	guchar *pg	= pe + w;
	guchar *ph	= pg + 1;
	guchar *pi	= ph + 1;

	for (x=0; x<width; x++)
	{
		for (i=0; i<bpp; i++) *po++ = 255;
	}

	for (y = 1; y<h; y++)
	{
		pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++; pi++;

		for (i=0; i<bpp; i++) *po++ = 255;

		for (x=1; x<w; x++)
		{
			v = (!*pe && (*pa==255 || *pb==255 || *pc==255 || *pd==255 || *pf==255 || *pg==255 || *ph==255 || *pi==255)) ? 0 : 255;

			for (i=0; i<channels; i++) *po++ = v;
			if (alpha) *po++ = 255;

			pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++; pi++;
		}
		pa++; pb++; pc++; pd++; pe++; pf++; pg++; ph++; pi++;

		for (i=0; i<bpp; i++) *po++ = 255;
	}

	for (x=0; x<width; x++)
	{
		for (i=0; i<bpp; i++) *po++ = 255;
	}
}


/*******************************************************/
/*                     Dialog                          */
/*******************************************************/


static gboolean edge_dialog(GimpDrawable *drawable)
{
	gboolean run;
	GtkObject *scale_data;
	GtkWidget *dialog, *box, *preview, *table, *check;

	gimp_ui_init("adaptive-edge", FALSE);

	dialog = gimp_dialog_new(
		"Adaptive Edge Detection",
		"gimp-adaptive-edge",
		NULL, 0,
		gimp_standard_help_func,
		"plug-in-adaptive-edge",
		GTK_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		NULL
	);

	gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog), GTK_RESPONSE_OK, GTK_RESPONSE_CANCEL, -1);
	gimp_window_set_transient(GTK_WINDOW(dialog));

	box = gtk_vbox_new(FALSE, 12);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE, 0);
	gtk_widget_show(box);

	preview = gimp_drawable_preview_new(drawable, NULL);
	gtk_box_pack_start(GTK_BOX(box), preview, TRUE, TRUE, 0);
	gtk_widget_set_sensitive(preview, !evals.automatic);
	gtk_widget_show(preview);

	g_signal_connect(preview, "invalidated", G_CALLBACK(edge_preview), NULL);

	check = gtk_check_button_new_with_mnemonic("A_utomatic thresholding (preview will be unavailable if enabled)");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), evals.automatic);
	gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
	gtk_widget_show(check);

	g_signal_connect(check, "toggled", G_CALLBACK(gimp_toggle_button_update), &evals.automatic);
	g_signal_connect_swapped(check, "toggled", G_CALLBACK(gimp_preview_invalidate), preview);

	table = gtk_table_new(1, 3, FALSE);
	gtk_table_set_col_spacings(GTK_TABLE(table), 6);
	gtk_box_pack_start (GTK_BOX(box), table, FALSE, FALSE, 0);
	gtk_widget_set_sensitive(table, !evals.automatic);
	gtk_widget_show (table);

	//  Label, scale, entry for evals.amount
	scale_data = gimp_scale_entry_new(
		GTK_TABLE(table), 0, 1, "T_hreshold:", 100, 0,
		evals.threshold, 1, 255, 1, 1, 1,
		FALSE, 1, G_MAXINT,
		NULL, NULL
	);

	g_signal_connect(scale_data, "value-changed", G_CALLBACK(gimp_int_adjustment_update), &evals.threshold);
	g_signal_connect_swapped(scale_data, "value-changed", G_CALLBACK (gimp_preview_invalidate), preview);

	gtk_object_set_data(GTK_OBJECT(check), "inverse_sensitive", table);
	gtk_object_set_data(GTK_OBJECT(table), "inverse_sensitive", preview);


	gtk_widget_show(dialog);
	run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);
	gtk_widget_destroy (dialog);

	return run;
}
