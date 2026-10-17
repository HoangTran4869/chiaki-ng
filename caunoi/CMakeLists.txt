# Cau noi cho app Dich Phu De Game (ban PS5) - dua tren chiaki-lib, AGPL-3.0
add_executable(cau-noi cau_noi.c)
target_link_libraries(cau-noi chiaki-lib)
if(APPLE)
	# 17/10: ban Mac - curl tinh + openssl can cac framework he thong
	target_link_libraries(cau-noi "-framework CoreFoundation" "-framework Security" "-framework SystemConfiguration"
		"-framework CoreServices" "-framework IOKit" "-framework AudioToolbox" "-framework VideoToolbox"
		"-framework CoreMedia" "-framework CoreVideo" z)
endif()
install(TARGETS cau-noi)
