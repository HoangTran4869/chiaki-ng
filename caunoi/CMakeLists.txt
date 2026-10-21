# Cau noi cho app Dich Phu De Game (ban PS5) - dua tren chiaki-lib, AGPL-3.0
add_executable(cau-noi cau_noi.c)
target_link_libraries(cau-noi chiaki-lib)
if(WIN32 OR APPLE)
	# 20/10: cua so hinh muot (21/10: ca Mac - Metal) (SDL2 ve bang card do hoa) - co SDL2 thi bat
	find_package(SDL2 CONFIG)
	if(SDL2_FOUND)
		target_compile_definitions(cau-noi PRIVATE CAUNOI_VIDEO)
		if(APPLE AND TARGET SDL2::SDL2)
			# 21/10 Mac: KHONG link SDL2 (nap bang dlopen luc bat hinh muot) -> chi lay thu muc header
			target_include_directories(cau-noi PRIVATE $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>)
		elseif(TARGET SDL2::SDL2)
			target_link_libraries(cau-noi SDL2::SDL2)
		else()
			target_include_directories(cau-noi PRIVATE ${SDL2_INCLUDE_DIRS})
			target_link_libraries(cau-noi ${SDL2_LIBRARIES})
		endif()
		message(STATUS "cau-noi: co cua so hinh muot (SDL2)")
	else()
		message(WARNING "cau-noi: KHONG tim thay SDL2 -> khong co cua so hinh muot")
	endif()
endif()
if(APPLE)
	# 17/10: ban Mac - curl tinh + openssl can cac framework he thong
	target_link_libraries(cau-noi "-framework CoreFoundation" "-framework Security" "-framework SystemConfiguration"
		"-framework CoreServices" "-framework IOKit" "-framework AudioToolbox" "-framework VideoToolbox"
		"-framework CoreMedia" "-framework CoreVideo" z objc)
endif()
install(TARGETS cau-noi)
