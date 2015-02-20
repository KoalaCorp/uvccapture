
/*******************************************************************************
#             uvccapture: USB UVC Video Class Snapshot Software                #
#This package work with the Logitech UVC based webcams with the mjpeg feature  #
#.                                                                             #
# 	Orginally Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard       #
#       Modifications Copyright (C) 2006  Gabriel A. Devenyi                   #
#                               (C) 2010  Alexandru Csete                      #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <jpeglib.h>
#include <time.h>
#include <linux/videodev2.h>

#include "v4l2uvc.h"

static const char version[] = VERSION;
int run = 1;

void sigcatch (int sig)
{
    fprintf (stderr, "Exiting...\n");
    run = 0;
}


/* This is used to execute the postprocessing command. */
int spawn (char *argv[], int wait, int verbose)
{
    pid_t pid;
    int rv;

    switch (pid = fork ()) {
    case -1:
        return -1;
    case 0:
        // CHILD
        execvp (argv[0], argv);
        fprintf (stderr, "Error executing command '%s'\n", argv[0]);
        exit (1);
    default:
        // PARENT
        if (wait == 1) {
            if (verbose >= 1)
                fprintf (stderr, "Waiting for command to finish...");
            waitpid (pid, &rv, 0);
            if (verbose >= 1)
                fprintf (stderr, "\n");
        } else {
            // Clean zombies
            waitpid (-1, &rv, WNOHANG);
            rv = 0;
        }
        break;
    }

    return rv;
}

int compress_yuyv_to_jpeg (struct vdIn *vd, FILE * file, int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    unsigned char *line_buffer, *yuyv;
    int z;

    fprintf (stderr, "Compressing YUYV frame to JPEG image.\n");

    line_buffer = calloc (vd->width * 3, 1);
    yuyv = vd->framebuffer;

    cinfo.err = jpeg_std_error (&jerr);
    jpeg_create_compress (&cinfo);
    jpeg_stdio_dest (&cinfo, file);

    cinfo.image_width = vd->width;
    cinfo.image_height = vd->height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults (&cinfo);
    jpeg_set_quality (&cinfo, quality, TRUE);

    jpeg_start_compress (&cinfo, TRUE);

    z = 0;
    while (cinfo.next_scanline < cinfo.image_height) {
        int x;
        unsigned char *ptr = line_buffer;

        for (x = 0; x < vd->width; x++) {
            int r, g, b;
            int y, u, v;

            if (!z)
                y = yuyv[0] << 8;
            else
                y = yuyv[2] << 8;
            u = yuyv[1] - 128;
            v = yuyv[3] - 128;

            r = (y + (359 * v)) >> 8;
            g = (y - (88 * u) - (183 * v)) >> 8;
            b = (y + (454 * u)) >> 8;

            *(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
            *(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
            *(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

            if (z++) {
                z = 0;
                yuyv += 4;
            }
        }

        row_pointer[0] = line_buffer;
        jpeg_write_scanlines (&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress (&cinfo);
    jpeg_destroy_compress (&cinfo);

    free (line_buffer);

    return (0);
}

int main (int argc, char *argv[])
{
    char *videodevice = "/dev/video0";
    char *pathfile = "/home/dipascual/Pictures/";
    char thisfile[200]; /* used as filename buffer in multi-file seq. */
    int format = V4L2_PIX_FMT_YUYV;
    int grabmethod = 1;
    int width = 320;
    int height = 240;
    int delay = 2;
    int skip = 0;
    int quality = 95;

    time_t ref_time;
    struct vdIn *videoIn;
    FILE *file;

    (void) signal (SIGINT, sigcatch);
    (void) signal (SIGQUIT, sigcatch);
    (void) signal (SIGKILL, sigcatch);
    (void) signal (SIGTERM, sigcatch);
    (void) signal (SIGABRT, sigcatch);
    (void) signal (SIGTRAP, sigcatch);

    fprintf (stderr, "Using videodevice: %s\n", videodevice);
    fprintf (stderr, "Image size: %dx%d\n", width, height);
    fprintf (stderr, "Taking snapshot every %d seconds\n", delay);
    fprintf (stderr, "Taking images using mmap\n");

    videoIn = (struct vdIn *) calloc (1, sizeof (struct vdIn));
    if (init_videoIn
        (videoIn, (char *) videodevice, width, height, format, grabmethod) < 0)
        exit (1);

    //Reset all camera controls
    fprintf (stderr, "Resetting camera settings\n");
    v4l2ResetControl (videoIn, V4L2_CID_BRIGHTNESS);
    v4l2ResetControl (videoIn, V4L2_CID_CONTRAST);
    v4l2ResetControl (videoIn, V4L2_CID_SATURATION);
    v4l2ResetControl (videoIn, V4L2_CID_GAIN);

    ref_time = time (NULL);

    while (run) {
        if (uvcGrab (videoIn) < 0) {
            fprintf (stderr, "Error grabbing\n");
            close_v4l2 (videoIn);
            free (videoIn);
            exit (1);
        }

        if (skip > 0) { skip--; continue; }

        if ((difftime (time (NULL), ref_time) > delay) || delay == 0) {
            sprintf (thisfile, "%ssnap_%lld.jpg", pathfile, (long long) ref_time);
            fprintf (stderr, "Saving image to: %s\n", thisfile);
            file = fopen (thisfile, "wb");
            
            if (file != NULL) {
                switch (videoIn->formatIn) {
                case V4L2_PIX_FMT_YUYV:
                    compress_yuyv_to_jpeg (videoIn, file, quality);
                    break;
                default:
                    fwrite (videoIn->tmpbuffer, videoIn->buf.bytesused + DHT_SIZE, 1,
                            file);
                    break;
                }
                fclose (file);
                videoIn->getPict = 0;
            }
            ref_time = time (NULL);
        }
    }
    close_v4l2 (videoIn);
    free (videoIn);

    return 0;
}
