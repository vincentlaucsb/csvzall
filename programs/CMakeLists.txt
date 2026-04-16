add_executable(csvzall_bind_string_copy ${CSVZALL_APP_SOURCES})
target_compile_definitions(csvzall_bind_string_copy
  PRIVATE
    CSVZALL_SQLITE_BIND_USE_STRING_COPY=1)
csvzall_configure_app(csvzall_bind_string_copy)

add_executable(csvzall_bind_text_length ${CSVZALL_APP_SOURCES})
target_compile_definitions(csvzall_bind_text_length
  PRIVATE
    CSVZALL_SQLITE_BIND_USE_STRING_COPY=0)
csvzall_configure_app(csvzall_bind_text_length)
