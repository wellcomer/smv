project(smv)

set(SOURCE_FILES smv.c)
set(MANPAGES smv.1)

set(DESTINATION_MAN_PAGE share/man/man1)
if(MANDIR)
    set(DESTINATION_MAN_PAGE ${MANDIR}/man1)
endif(MANDIR)

add_executable(smv ${SOURCE_FILES})

add_test(NAME test.sh COMMAND test.sh)
enable_testing()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(FILES ${MANPAGES} DESTINATION ${DESTINATION_MAN_PAGE})

