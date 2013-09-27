/* --------------------------------------------
Copyright (c) 2013, University of Granada
Copyright (c) 2013, Real-Time Innovations, Inc.
Copyright (c) 2013, Javier Povedano-Molina
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met: 

1. Redistributions of source code must retain the above copyright notices, this
   list of conditions and the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notices,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies, 
either expressed or implied, of the copyright holders or contributors.

This work was partially completed at the University of Granada under funding by 
Real-Time Innovations, Inc. (RTI).  The above license is granted with
 the permission of RTI.

-------------------------------------------- */
#include <fcntl.h>

#include <glib-object.h>
#include <string.h>

#include "VideoOutput.h"


// ----------------------------------------------------------------------------
void EMDSVideoOutput::Initialize()
{
	_frameHandler = NULL;
}

// ----------------------------------------------------------------------------
EMDSVideoOutput::EMDSVideoOutput()
{
	gst_init(NULL,NULL);
	Initialize();

}

// ----------------------------------------------------------------------------
void EMDSVideoOutput::PushFrame(EMDSBuffer *buffer)
{

	if (_frameHandler != NULL)
	{
		_frameHandler->FrameReady(this,buffer);
	}

	return;
}

// ----------------------------------------------------------------------------
static gboolean bus_eos_callback(
	GstBus *bus, GstMessage *message, gpointer data)
{
	EMDSVideoDisplayOutput *vidOut = 
		(EMDSVideoDisplayOutput *)data;

	/* end-of-stream */
	vidOut->GetFrameHandler()->EOSHandler(vidOut, NULL);

  /* we want to be notified again the next time there is a message
   * on the bus, so returning TRUE (FALSE means we want to stop watching
   * for messages on the bus and our callback should not be called again)
   */
  return TRUE;
}
// ----------------------------------------------------------------------------
static GstBusSyncReply bus_sync_handler(
	GstBus *bus, GstMessage *message, gpointer user_data) 
{
	GstElement *outwin = NULL;
	GValue *val = (GValue *)g_value_array_new(1);

	outwin = gst_bin_get_by_name((GstBin*)user_data,"sink");

	if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
		return GST_BUS_PASS;

	if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
		return GST_BUS_PASS;

	g_value_init(val,G_TYPE_BOOLEAN);
	g_value_set_boolean(val,FALSE);

	if (outwin != NULL)
	{	
		gst_child_proxy_set_property((GstObject *)(outwin),"sync",val);
	}

	gst_message_unref(message);

	return GST_BUS_DROP;
}


// ----------------------------------------------------------------------------
void EMDSVideoDisplayOutput::Initialize()
{
	EMDSVideoOutput::Initialize();
	GstBus *bus = NULL;

	_frameHandler = 
		new DisplayFrameHandler(this);

	// Create the video pipeline on Windows (sending to DirectDraw)
#ifdef WIN32

	_displayPipeline =
		(GstPipeline *)gst_parse_launch(
		"appsrc name=\"src\" is-live=\"true\" do-timestamp=\"true\" "
		"caps=\"video/x-vp8, width=(int)640, height=(int)360, "
		"pixel-aspect-ratio=(fraction)1/1, framerate=(fraction)1000/1\" ! "
		"queue2 ! vp8dec ! queue2 ! "
	//	"videorate ! video/x-raw-yuv,framerate=25/1 ! "
		"ffmpegcolorspace ! "
		"directdrawsink name=\"sink\"",
		NULL);

#else

	// Create the video pipeline on Linux (sending to XImageSink)
	_displayPipeline = 
		(GstPipeline *)gst_parse_launch(
		"appsrc name=\"src\" is-live=\"true\" do-timestamp=\"true\" "
		"caps=\"video/x-vp8, width=(int)640, height=(int)360, "
		"framerate=25/1\" ! queue2 ! "
		" vp8dec ! ffmpegcolorspace ! ximagesink sync=\"false\" ",
		NULL);

#endif

	// If the video pipeline was not created correctly, exit. 
	// The common causes for this include:
	//   - Plugins referenced by name above not found due to:
	//     - GStreamer not installed correctly, or
	//     - GStreamer not finding plugins due to missing GST_PLUGIN_PATH, or
	//     - Some plugins referenced above not available on your platform, or
	//     - Source code change to load different plugins
	if (_displayPipeline == NULL)
	{
		printf("Error: Failed to create pipeline.  Are gstreamer "
			"plugins installed correctly?\n");
		return;
	}

	// Add a sync handler to the bus
	bus = gst_pipeline_get_bus(
		GST_PIPELINE (_displayPipeline));
	gst_bus_set_sync_handler (bus, 
		(GstBusSyncHandler) bus_sync_handler, _displayPipeline);

	// Add a watch for EOS events
	gst_bus_add_signal_watch(bus);
	g_signal_connect(bus, "message::eos", G_CALLBACK (bus_eos_callback), this);
	gst_object_unref(bus);

	// Set the pipeline state to playing, so it actually displays video
	gst_element_set_state((GstElement*)_displayPipeline,
		GST_STATE_PLAYING);
}

// ----------------------------------------------------------------------------
EMDSVideoDisplayOutput::EMDSVideoDisplayOutput()
{

	Initialize();
	
	if (_displayPipeline == NULL)
	{
		printf("Failed to create output");
		return;
	} else 
	{
		gst_element_set_state((GstElement*)_displayPipeline,
			GST_STATE_PLAYING);
	}
}

// ----------------------------------------------------------------------------
std::string EMDSVideoDisplayOutput::GetStreamMetadata()
{
	GstElement *appSrc = gst_bin_get_by_name( GST_BIN(_displayPipeline), 
		"src");
	GstPad *srcPad = gst_element_get_static_pad(appSrc, "src");
	GstCaps *caps = gst_pad_get_allowed_caps(srcPad);

	char *capsStr = gst_caps_to_string(caps);

	return capsStr;
}


#ifndef WIN32

// ----------------------------------------------------------------------------
void
_frame_process_write_trace_fn(EMDSVideoOutput *self,EMDSBuffer *buffer){
	EMDSVideoTraceOutput *tracer = (EMDSVideoTraceOutput *)self;
	unsigned long seqn = 0;
	int written = 0;
	char line[1024];
	struct timeval recvtime;
	gettimeofday(&recvtime, NULL);
	seqn = buffer->GetSeqn();
	sprintf(line,
		"%lf %lf %lu %lu\n",
		buffer->GetTimestamp(),
		(double)recvtime.tv_sec + (recvtime.tv_usec/1000000.0),
		seqn, buffer->GetSize());
	written = write(tracer->_fd, line, strlen(line));
	if (written == 0){
		printf("Failed to write to trace file\n");
	}
}

// ----------------------------------------------------------------------------
EMDSVideoTraceOutput::EMDSVideoTraceOutput(std::string path)
{
	int fd;
	fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0600);
	if (fd < 1)
	{
		printf("Failed to open trace file '%s'\n", path.c_str());
	}

	EMDSVideoOutput::Initialize();
	_fd = fd;
	_filepath = strdup(path.c_str());
}
#endif


