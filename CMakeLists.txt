cmake_minimum_required(VERSION 3.16)
project(Voryel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network)

# En Windows embebemos un manifest que declara DPI-awareness (PerMonitorV2).
# Sin esto, Windows reescala la ventana como si fuera una imagen en pantallas
# con escalado (125%/150%/etc), causando bordes dentados y desalineados en
# todos los paneles/sombras dibujados por Qt.
set(PLATFORM_SOURCES "")
if (WIN32 AND MINGW)
    set(PLATFORM_SOURCES app.rc)
endif()

add_executable(Voryel
    main.cpp
    IconUtil.cpp
    SoundUtil.cpp
    SolidPanel.cpp
    Sidebar.cpp
    ToggleSwitch.cpp
    TitleBar.cpp
    BottomBar.cpp
    DashboardView.cpp
    ProjectView.cpp
    ChatView.cpp
    SettingsView.cpp
    MainWindow.cpp
    core/VoryelCore.cpp
    core/TokenCounter.cpp
    TokenRadialMeter.cpp
    model/ProviderUrlSecurity.cpp
    model/OpenAIClient.cpp
    model/ChatSession.cpp
    model/PromptBuilder.cpp
    chat/ChatStore.cpp
    resources.qrc
    ${PLATFORM_SOURCES}
)

target_link_libraries(Voryel PRIVATE Qt6::Widgets Qt6::Svg Qt6::Network winmm)
if (WIN32)
    target_link_libraries(Voryel PRIVATE crypt32)
endif()

# Copy sounds directory next to the executable
add_custom_command(TARGET Voryel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/sounds"
        "$<TARGET_FILE_DIR:Voryel>/sounds"
    COMMENT "Copying sounds to build output"
)
