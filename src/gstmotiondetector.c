/*
 * gstmotiondetector.c
 * Copyright Matthew Brush <mbrush@codebrainz.ca>
 * See COPYING file for license information.
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <cv.h>

#include "gstmotiondetector.h"


GST_DEBUG_CATEGORY_STATIC (gst_motion_detector_debug);
#define GST_CAT_DEFAULT gst_motion_detector_debug


#define DEFAULT_MOTION_DETECTED   FALSE
#define DEFAULT_POST_MESSAGES     FALSE
#define DEFAULT_DRAW_MOTION       FALSE
#define DEFAULT_AVG_WEIGHT        0.02
#define DEFAULT_THRESHOLD         70
#define DEFAULT_DILATE_ITERATIONS 18
#define DEFAULT_ERODE_ITERATIONS  10
#define DEFAULT_MIN_BLOB_SIZE     10
#define DEFAULT_MAX_BLOB_SIZE     255
#define DEFAULT_NUM_BLOBS         0
#define DEFAULT_RATE_LIMIT        500


enum
{
  LAST_SIGNAL
};


enum
{
  PROP_0,
  PROP_MOTION_DETECTED,
  PROP_POST_MESSAGES,
  PROP_DRAW_MOTION,
  PROP_AVG_WEIGHT,
  PROP_THRESHOLD,
  PROP_DILATE_ITERATIONS,
  PROP_ERODE_ITERATIONS,
  PROP_MINIMUM_BLOB_SIZE,
  PROP_MAXIMUM_BLOB_SIZE,
  PROP_NUM_BLOBS,
  PROP_RATE_LIMIT
};


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb,bpp=24,depth=24,endianess=4321")
    );


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb,bpp=24,depth=24,endianess=4321")
    );


GST_BOILERPLATE (GstMotionDetector, gst_motion_detector, GstElement,
    GST_TYPE_ELEMENT);


static void gst_motion_detector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_motion_detector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_motion_detector_finalize (GObject *obj);

static gboolean gst_motion_detector_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_motion_detector_chain (GstPad * pad, GstBuffer * buf);

static GstBuffer *gst_motion_detector_buffer_set_from_ipl_image (GstBuffer *buf, IplImage *img);
static IplImage *gst_motion_detector_process_image (GstMotionDetector *filter, IplImage *src);


static void
gst_motion_detector_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "MotionDetector",
    "Analyzer/Video",
    "Detects motion in video streams",
    "Matthew Brush <mbrush@codebrainz.ca>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}


static void
gst_motion_detector_class_init (GstMotionDetectorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_motion_detector_set_property;
  gobject_class->get_property = gst_motion_detector_get_property;
  gobject_class->finalize = gst_motion_detector_finalize;

  g_object_class_install_property (gobject_class, PROP_MOTION_DETECTED,
    g_param_spec_boolean ("motion-detected", "MotionDetected",
      "Whether or not motion was detected", DEFAULT_MOTION_DETECTED,
      G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_POST_MESSAGES,
    g_param_spec_boolean ("post-messages", "PostMessages",
      "Whether or not to post messages on the bus", DEFAULT_POST_MESSAGES,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_DRAW_MOTION,
    g_param_spec_boolean ("draw-motion", "DrawMotion",
      "Whether or not to draw areas where motion was detected.", DEFAULT_DRAW_MOTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_AVG_WEIGHT,
    g_param_spec_double ("avg-weight", "AvgWeight",
      "Weight new frames are given when added to the running average",
      0.0, 1.0, DEFAULT_AVG_WEIGHT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
    g_param_spec_uint ("threshold", "Threshold",
      "The threshold level used when converting to a binary image",
      0, 255, DEFAULT_THRESHOLD, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_DILATE_ITERATIONS,
    g_param_spec_uint ("dilate-iterations", "DilateIterations",
      "Number of times the binary image is dilated",
      0, 255, DEFAULT_DILATE_ITERATIONS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_ERODE_ITERATIONS,
    g_param_spec_uint ("erode-iterations", "ErodeIterations",
      "Number of times the binary image is eroded",
      0, 255, DEFAULT_ERODE_ITERATIONS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_MINIMUM_BLOB_SIZE,
    g_param_spec_uint ("minimum-blob-size", "MinimumBlobSize",
      "Minimum height or width of blob to be considered",
      0, 255, DEFAULT_MIN_BLOB_SIZE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_MAXIMUM_BLOB_SIZE,
    g_param_spec_uint ("maximum-blob-size", "MaximumBlobSize",
      "Maximum height or width of blob to be considered",
      0, 255, DEFAULT_MAX_BLOB_SIZE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_NUM_BLOBS,
    g_param_spec_uint ("num-blobs", "NumBlobs",
      "Number of blobs in the frame", 0, 255, DEFAULT_NUM_BLOBS,
      G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_RATE_LIMIT,
    g_param_spec_uint ("rate-limit", "RateLimit",
      "Number of milliseconds before allowing another detection",
      0, G_MAXUINT, DEFAULT_RATE_LIMIT,  G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}


static void
gst_motion_detector_init (GstMotionDetector * filter, GstMotionDetectorClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_motion_detector_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_motion_detector_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
                                GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->motion_detected = DEFAULT_MOTION_DETECTED;
  filter->post_messages = DEFAULT_POST_MESSAGES;
  filter->draw_motion = DEFAULT_DRAW_MOTION;
  filter->avg_weight = DEFAULT_AVG_WEIGHT;
  filter->threshold = DEFAULT_THRESHOLD;
  filter->dilate_iterations = DEFAULT_DILATE_ITERATIONS;
  filter->erode_iterations = DEFAULT_ERODE_ITERATIONS;
  filter->min_blob_size = DEFAULT_MIN_BLOB_SIZE;
  filter->max_blob_size = DEFAULT_MAX_BLOB_SIZE;
  filter->num_blobs = DEFAULT_NUM_BLOBS;
  filter->rate_limit = DEFAULT_RATE_LIMIT;
  filter->rate_inhibit = FALSE;
  filter->run_avg = NULL;
}


static void
gst_motion_detector_finalize (GObject *obj)
{
  GstMotionDetector *filter;
  GstMotionDetectorClass *klass;
  GstElementClass *parent_class;

  filter = GST_MOTION_DETECTOR (obj);
  klass = G_TYPE_INSTANCE_GET_CLASS (filter, GST_TYPE_MOTION_DETECTOR, GstMotionDetectorClass);
  parent_class = g_type_class_peek_parent (klass);

  if (filter->run_avg)
    cvReleaseImage (&(filter->run_avg));

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}


static void
gst_motion_detector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMotionDetector *filter = GST_MOTION_DETECTOR (object);

  switch (prop_id)
    {
    case PROP_POST_MESSAGES:
      filter->post_messages = g_value_get_boolean (value);
      break;
    case PROP_DRAW_MOTION:
      filter->draw_motion = g_value_get_boolean (value);
      break;
    case PROP_AVG_WEIGHT:
      filter->avg_weight = g_value_get_double (value);
      break;
    case PROP_THRESHOLD:
      filter->threshold = g_value_get_uint (value);
      break;
    case PROP_DILATE_ITERATIONS:
      filter->dilate_iterations = g_value_get_uint (value);
      break;
    case PROP_ERODE_ITERATIONS:
      filter->erode_iterations = g_value_get_uint (value);
      break;
    case PROP_MINIMUM_BLOB_SIZE:
      filter->min_blob_size = g_value_get_uint (value);
      break;
    case PROP_MAXIMUM_BLOB_SIZE:
      filter->max_blob_size = g_value_get_uint (value);
      break;
    case PROP_RATE_LIMIT:
      filter->rate_limit = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
gst_motion_detector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMotionDetector *filter = GST_MOTION_DETECTOR (object);

  switch (prop_id)
    {
    case PROP_MOTION_DETECTED:
      g_value_set_boolean (value, filter->motion_detected);
      break;
    case PROP_POST_MESSAGES:
      g_value_set_boolean (value, filter->post_messages);
      break;
    case PROP_DRAW_MOTION:
      g_value_set_boolean (value, filter->draw_motion);
      break;
    case PROP_AVG_WEIGHT:
      g_value_set_double (value, filter->avg_weight);
      break;
    case PROP_THRESHOLD:
      g_value_set_uint (value, filter->threshold);
      break;
    case PROP_DILATE_ITERATIONS:
      g_value_set_uint (value, filter->dilate_iterations);
      break;
    case PROP_ERODE_ITERATIONS:
      g_value_set_uint (value, filter->erode_iterations);
      break;
    case PROP_MINIMUM_BLOB_SIZE:
      g_value_set_uint (value, filter->min_blob_size);
      break;
    case PROP_MAXIMUM_BLOB_SIZE:
      g_value_set_uint (value, filter->max_blob_size);
      break;
    case PROP_NUM_BLOBS:
      g_value_set_uint (value, filter->num_blobs);
      break;
    case PROP_RATE_LIMIT:
      g_value_set_uint (value, filter->rate_limit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static gboolean
gst_motion_detector_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMotionDetector *filter;
  GstPad *otherpad;
  gint width, height;
  GstStructure *structure;

  filter = GST_MOTION_DETECTOR (gst_pad_get_parent (pad));
  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  filter->width=width;
  filter->height=height;
  filter->currentImage = cvCreateImage(cvSize(width, height), IPL_DEPTH_8U, 3);

  return gst_pad_set_caps (otherpad, caps);
}


static gboolean
gst_motion_detector_rate_timeout (GstMotionDetector *filter)
{
  filter->rate_inhibit = FALSE;
  return FALSE;
}


static GstFlowReturn
gst_motion_detector_chain (GstPad * pad, GstBuffer * buf)
{
  GstMotionDetector *filter = GST_MOTION_DETECTOR (GST_OBJECT_PARENT (pad));

  /*filter->currentImage = gst_motion_detector_gst_buffer_to_ipl_image (buf, filter);*/
  filter->currentImage->imageData = (char *) GST_BUFFER_DATA (buf);

  filter->currentImage = gst_motion_detector_process_image (filter, filter->currentImage);

  if (filter->draw_motion)
    gst_motion_detector_buffer_set_from_ipl_image (buf, filter->currentImage);

  if (!filter->rate_inhibit)
    {
      GstBus *bus;
      GstMessage *msg;

      bus = gst_element_get_bus (GST_ELEMENT (filter));
      if ((filter->num_blobs > 0) && (!filter->motion_detected))
      {
        filter->motion_detected = TRUE;
        g_object_notify (G_OBJECT (filter), "motion-detected");
        if (filter->post_messages)
        {
          msg = gst_message_new_application (GST_OBJECT (filter),
            gst_structure_new ("motion-data",
            "motion-detected", G_TYPE_BOOLEAN, TRUE,
            "num-blobs", G_TYPE_UINT, filter->num_blobs, NULL));
          gst_bus_post (bus, msg);
        }
      }
      else if (filter->motion_detected)
      {
        filter->motion_detected = FALSE;
        g_object_notify (G_OBJECT (filter), "motion-detected");
        if (filter->post_messages)
        {
          msg = gst_message_new_application (GST_OBJECT (filter),
            gst_structure_new ("motion-data",
              "motion-detected", G_TYPE_BOOLEAN, FALSE,
              "num-blobs", G_TYPE_UINT, 0, NULL));
          gst_bus_post (bus, msg);
        }
      }

      if (filter->rate_limit > 0)
      {
        filter->rate_inhibit = TRUE;
        g_timeout_add (filter->rate_limit,
          (GSourceFunc) gst_motion_detector_rate_timeout, filter);
      }

      gst_object_unref (bus);
    }

  return gst_pad_push (filter->srcpad, buf);
}


static GstBuffer *
gst_motion_detector_buffer_set_from_ipl_image (GstBuffer *buf, IplImage *img)
{
  gst_buffer_set_data(buf, (guint8 *)img->imageData, img->imageSize);
  return buf;
}


static IplImage *
gst_motion_detector_process_image (GstMotionDetector *filter, IplImage *src)
{
  gint num_blobs = 0;
  IplImage *diff, *tmp, *gr;
  CvMemStorage *store = cvCreateMemStorage (0);
  CvSeq *contour = NULL;
  CvRect bind_rect = cvRect (0, 0, 0, 0);

  if (!filter->run_avg)
    {
      filter->run_avg = cvCreateImage (cvGetSize (src), IPL_DEPTH_32F, 3);
      cvConvertScale (src, filter->run_avg, 1.0, 0.0);
    }
  else
    cvRunningAvg(src, filter->run_avg, filter->avg_weight, NULL);

  tmp = cvCreateImage (cvGetSize (src), IPL_DEPTH_8U, 3);
  cvConvertScale (filter->run_avg, tmp, 1.0, 0.0);

  diff = cvCreateImage (cvGetSize (src), IPL_DEPTH_8U, 3);
  cvAbsDiff (src, tmp, diff);

  gr = cvCreateImage (cvGetSize (src), IPL_DEPTH_8U, 1);
  cvCvtColor (diff, gr, CV_RGB2GRAY);

  cvThreshold (gr, gr, filter->threshold, 255, CV_THRESH_BINARY);
  cvDilate (gr, gr, 0, filter->dilate_iterations);
  cvErode (gr, gr, 0,filter->erode_iterations);

  cvFindContours (gr, store, &contour, sizeof (CvContour),
    CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE, cvPoint (0, 0));

  for (; contour != NULL; contour = contour->h_next)
  {
    bind_rect = cvBoundingRect (contour, 0);

    if (bind_rect.width < filter->min_blob_size ||
      bind_rect.height < filter->min_blob_size ||
      bind_rect.width > filter->max_blob_size ||
      bind_rect.height > filter->max_blob_size)
    {
      continue;
    }
    num_blobs++;

    if (filter->post_messages)
    {
      GstStructure *s = gst_structure_new ("motion",
        "x", G_TYPE_UINT, bind_rect.x,
        "y", G_TYPE_UINT, bind_rect.y,
        "width", G_TYPE_UINT, bind_rect.width,
        "height", G_TYPE_UINT, bind_rect.height, NULL);

      GstMessage *m = gst_message_new_element (GST_OBJECT (filter), s);
      gst_element_post_message (GST_ELEMENT (filter), m);
    }
    if (filter->draw_motion)
    {
      cvRectangle (src, cvPoint (bind_rect.x, bind_rect.y),
        cvPoint (bind_rect.x + bind_rect.width,
          bind_rect.y + bind_rect.height),
        CV_RGB (255, 0, 0), 1, 8, 0);
    }
  }
  cvReleaseMemStorage(&store);
  cvReleaseImage (&tmp);
  cvReleaseImage (&diff);
  cvReleaseImage (&gr);

  filter->num_blobs = num_blobs;

  return src;
}


static gboolean
motiondetector_init (GstPlugin * motiondetector)
{
  GST_DEBUG_CATEGORY_INIT (gst_motion_detector_debug, "motiondetector",
      0, "Detects motion in video streams");

  return gst_element_register (motiondetector, "motiondetector", GST_RANK_NONE,
      GST_TYPE_MOTION_DETECTOR);
}


#ifndef PACKAGE
#define PACKAGE "motiondetector"
#endif


GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "motiondetector",
    "Detects motion in video streams",
    motiondetector_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
