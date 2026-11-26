# Native Qt port of the Half-Life Model Viewer; upstream/ holds the original Win32 sources for reference only
add_executable(hlmv main.cpp mdlviewer.cpp controlpanel.cpp mx.cpp)
target_link_libraries(hlmv PRIVATE sfm_viewport Qt6::Widgets)
