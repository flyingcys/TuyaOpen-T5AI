set(incs .)

set(srcs)

if (CONFIG_ASDF_ONBOARD_MIC_STREAM)

list(APPEND srcs
		onboard_mic_stream.c
	)

if (CONFIG_ASDF_ONBOARD_MIC_STREAM_TEST)
	list(APPEND srcs
		onboard_mic_stream_test.c
	)
endif()

endif()

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES bk_common audio_pipeline fatfs_stream)