cmake_minimum_required(VERSION 3.18.4...3.28.0)

project(Pyrogenesis DESCRIPTION "Pyrogenesis, a RTS Engine" LANGUAGES NONE)

# Check if Doxygen and graphviz are installed.
find_package(Doxygen 1.9.1 REQUIRED dot)

if(DOXYGEN_FOUND)

    include(FetchContent)

    FetchContent_Declare(doxygen_awesome_css
        GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css
        GIT_TAG        v2.3.3
        SOURCE_DIR     ${CMAKE_CURRENT_BINARY_DIR}/styling
    )
    FetchContent_MakeAvailable(doxygen_awesome_css)

    # Get current Branch Name to set it as the Project Number.
    find_package(Git)
    if(Git_FOUND)
        set(ENV{GIT_DISCOVERY_ACROSS_FILESYSTEM} 1)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree OUTPUT_VARIABLE IS_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(IS_GIT)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD OUTPUT_VARIABLE CURRENT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
    endif()

    # Doxygen Configuration.
    if(CURRENT_BRANCH)
        set(DOXYGEN_PROJECT_NUMBER ${CURRENT_BRANCH})
    else()
        set(DOXYGEN_PROJECT_NUMBER main)
    endif()
    set(DOXYGEN_PROJECT_LOGO ${CMAKE_CURRENT_SOURCE_DIR}/pyrogenesis.png)
    set(DOXYGEN_TAB_SIZE 4)
    set(DOXYGEN_EXCLUDE_PATTERNS */.svn* */tests/test_*)
    set(DOXYGEN_INCLUDE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../source)
    set(DOXYGEN_EXAMPLE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../source)
    set(DOXYGEN_EXCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../source/tools ${CMAKE_CURRENT_SOURCE_DIR}/../../source/third_party)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_HTML_EXTRA_STYLESHEET ${doxygen_awesome_css_SOURCE_DIR}/doxygen-awesome.css ${CMAKE_CURRENT_SOURCE_DIR}/style.css)
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_EXTRACT_ANON_NSPACES YES)
    set(DOXYGEN_SHOW_DIRECTORIES YES)
    set(DOXYGEN_STRIP_CODE_COMMENTS NO)
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
    set(DOXYGEN_GENERATE_TODOLIST NO)
    set(DOXYGEN_PREDEFINED "UNUSED(x)=x" "METHODDEF(x)=static x" "GLOBAL(x)=x")
    set(DOXYGEN_EXPAND_AS_DEFINED DEFAULT_COMPONENT_ALLOCATOR DEFAULT_SCRIPT_WRAPPER DEFAULT_INTERFACE_WRAPPER DEFAULT_MESSAGE_IMPL MESSAGE INTERFACE COMPONENT GUISTDTYPE)
    set(DOXYGEN_WARN_LOGFILE doxygen.log)

    doxygen_add_docs(${CMAKE_PROJECT_NAME}
             ${CMAKE_CURRENT_SOURCE_DIR}/../../source
             ${CMAKE_CURRENT_SOURCE_DIR}/mainpage.dox
             ${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE.md
            ALL)
else()
    message(SEND_ERROR "Make sure Doxygen is installed and usable")
endif()
