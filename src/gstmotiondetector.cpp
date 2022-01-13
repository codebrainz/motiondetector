/*
 * gstmotiondetector.c
 * Copyright Matthew Brush <mbrush@codebrainz.ca>
 * See COPYING file for license information.
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <opencv2/imgproc/imgproc_c.h>

#include "gstmotiondetector.hpp"


GST_DEBUG_CATEGORY (gst_motion_detector_debug);
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
    GST_STATIC_CAPS ("video/x-raw,bpp=24,depth=24,format=RGB")
    );


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,bpp=8,depth=8,format=GRAY8")
    );


#define gst_motion_detector_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMotionDetector, gst_motion_detector, GST_TYPE_ELEMENT,
                         GST_DEBUG_CATEGORY_INIT(gst_motion_detector_debug, "gstmotiondetector", 0,
                                                 "debug category for gstmotiondetector element"));


static void gst_motion_detector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_motion_detector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_motion_detector_finalize (GObject *obj);

static GstFlowReturn gst_motion_detector_chain (GstPad *pad, GstObject *parent, GstBuffer *buf);
static gboolean
gst_motion_detector_event(GstPad *pad, GstObject * parent, GstEvent * event);

static IplImage *gst_motion_detector_process_image (GstMotionDetector *filter, IplImage *src);


static void
gst_motion_detector_class_init (GstMotionDetectorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
                                          "MotionDetector", "Analyzer/Video", "Detects motion in video streams",
                                          "Matthew Brush <mbrush@codebrainz.ca>, Alexey Gornostaev <kreopt@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));


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
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DRAW_MOTION,
    g_param_spec_boolean ("draw-motion", "DrawMotion",
      "Whether or not to draw areas where motion was detected.", DEFAULT_DRAW_MOTION,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_AVG_WEIGHT,
    g_param_spec_double ("avg-weight", "AvgWeight",
      "Weight new frames are given when added to the running average",
      0.0, 1.0, DEFAULT_AVG_WEIGHT, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
    g_param_spec_uint ("threshold", "Threshold",
      "The threshold level used when converting to a binary image",
      0, 255, DEFAULT_THRESHOLD, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DILATE_ITERATIONS,
    g_param_spec_uint ("dilate-iterations", "DilateIterations",
      "Number of times the binary image is dilated",
      0, 255, DEFAULT_DILATE_ITERATIONS, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_ERODE_ITERATIONS,
    g_param_spec_uint ("erode-iterations", "ErodeIterations",
      "Number of times the binary image is eroded",
      0, 255, DEFAULT_ERODE_ITERATIONS, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_MINIMUM_BLOB_SIZE,
    g_param_spec_uint ("minimum-blob-size", "MinimumBlobSize",
      "Minimum height or width of blob to be considered",
      0, 255, DEFAULT_MIN_BLOB_SIZE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_MAXIMUM_BLOB_SIZE,
    g_param_spec_uint ("maximum-blob-size", "MaximumBlobSize",
      "Maximum height or width of blob to be considered",
      0, 255, DEFAULT_MAX_BLOB_SIZE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_NUM_BLOBS,
    g_param_spec_uint ("num-blobs", "NumBlobs",
      "Number of blobs in the frame", 0, 255, DEFAULT_NUM_BLOBS,
      G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_RATE_LIMIT,
    g_param_spec_uint ("rate-limit", "RateLimit",
      "Number of milliseconds before allowing another detection",
      0, G_MAXUINT, DEFAULT_RATE_LIMIT,  (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));
}


static void
gst_motion_detector_init (GstMotionDetector * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_motion_detector_chain));
  gst_pad_set_event_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_motion_detector_event));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");

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
  filter = GST_MOTION_DETECTOR (obj);

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
gst_motion_detector_event(GstPad *pad, GstObject * parent, GstEvent * event)
{
    gboolean ret = TRUE;

    switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_CAPS: {

            GstMotionDetector *filter = GST_MOTION_DETECTOR (parent);
            //GstPad *otherpad;
            gint width, height, fps0, fps1;
            GstStructure *structure;
            GstCaps *caps, *out_caps;

            gst_event_parse_caps (event, &caps);

            //otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

            structure = gst_caps_get_structure (caps, 0);
            gst_structure_get_int (structure, "width", &width);
            gst_structure_get_int (structure, "height", &height);
            gst_structure_get_fraction (structure, "framerate", &fps0, &fps1);

            filter->width=width;
            filter->height=height;
            filter->currentImage = cvCreateImageHeader(cvSize(width, height), IPL_DEPTH_8U, 3);


            out_caps = gst_caps_new_simple("video/x-raw",
               "format", G_TYPE_STRING, "GRAY8",
               "framerate", GST_TYPE_FRACTION, fps0, fps1,
               "width", G_TYPE_INT, width,
               "height", G_TYPE_INT, height,
               "bpp", G_TYPE_INT, 8,
               "depth", G_TYPE_INT, 8,
               NULL);

            ret = gst_pad_set_caps (/*otherpad*/filter->srcpad, out_caps);

            break;
        };
        default:
            ret = gst_pad_event_default (pad, parent, event);
            break;
    }

    return ret;
}


static gboolean
gst_motion_detector_rate_timeout (GstMotionDetector *filter)
{
  filter->rate_inhibit = FALSE;
  return FALSE;
}

static void
free_iplimg(IplImage *img) {
  cvReleaseImage (&img);
}


/*
static GstBuffer *
gst_motion_detector_buffer_set_from_ipl_image (GstBuffer *buf, IplImage *img)
{
  gst_buffer_set_data(buf, (guint8 *)img->imageData, img->imageSize);
  return buf;
}
*/

static GstFlowReturn
gst_motion_detector_chain (GstPad *pad,
                            GstObject *parent,
                            GstBuffer *buf)
{
  GstMotionDetector *filter = GST_MOTION_DETECTOR (GST_OBJECT_PARENT (pad));

  /*filter->currentImage = gst_motion_detector_gst_buffer_to_ipl_image (buf, filter);*/
  GstMapInfo info;
  gst_buffer_map (buf, &info, GST_MAP_READ);

  filter->currentImage->imageData = (char *) info.data;

  IplImage* gray = cvCreateImage (cvSize(filter->currentImage->width, filter->currentImage->height), IPL_DEPTH_8U, 1);
  cvCvtColor (filter->currentImage, gray, CV_RGB2GRAY);

  gst_motion_detector_process_image (filter, gray);

  gst_buffer_unmap (buf, &info);

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

    gst_buffer_unref(buf);
    GstBuffer *new_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, gray->imageData,
                                                   gray->imageSize, 0,
                                                   gray->imageSize,
                                                   gray,
                                                   (GDestroyNotify)free_iplimg);

    return gst_pad_push (filter->srcpad, new_buf);
}

static IplImage *
gst_motion_detector_process_image (GstMotionDetector *filter, IplImage *src)
{
  gint num_blobs = 0;
  IplImage *tmp, *gr;
  CvMemStorage *store = cvCreateMemStorage (0);
  CvSeq *contour = NULL;
  CvRect bind_rect = cvRect (0, 0, 0, 0);

  if (!filter->run_avg) {
      filter->run_avg = cvCreateImage (cvGetSize (src), IPL_DEPTH_32F, 1);
      cvConvertScale (src, filter->run_avg, 1.0, 0.0);
  } else {
    cvRunningAvg(src, filter->run_avg, filter->avg_weight, NULL);
  }


  tmp = cvCreateImage (cvGetSize (src), IPL_DEPTH_8U, 1);
  cvConvertScale (filter->run_avg, tmp, 1.0, 0.0);

  gr = cvCreateImage (cvGetSize (src), IPL_DEPTH_8U, 1);
  cvAbsDiff (src, tmp, gr);

  cvThreshold (gr, gr, filter->threshold, 255, CV_THRESH_BINARY);
  cvDilate (gr, gr, 0, filter->dilate_iterations);
  cvErode (gr, gr, 0,filter->erode_iterations);

  cvFindContours (gr, store, &contour, sizeof (CvContour),
    CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE, cvPoint (0, 0));
  for (; contour != NULL; contour = contour->h_next)
  {
    bind_rect = cvBoundingRect (contour, 0);

    if (bind_rect.width < (gint) filter->min_blob_size ||
      bind_rect.height < (gint) filter->min_blob_size ||
      bind_rect.width > (gint) filter->max_blob_size ||
      bind_rect.height > (gint) filter->max_blob_size)
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
        {255,0,0}, 1, 8, 0);
    }
  }
  cvReleaseMemStorage(&store);
  cvReleaseImage (&tmp);

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
    motiondetector,
    "Detects motion in video streams",
    motiondetector_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
