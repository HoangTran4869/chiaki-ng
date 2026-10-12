# Cau noi cho app Dich Phu De Game (ban PS5) - dua tren chiaki-lib, AGPL-3.0
add_executable(cau-noi cau_noi.c)
target_link_libraries(cau-noi chiaki-lib)
install(TARGETS cau-noi)
