# Working title. Thousands of animated 8-direction figures with DNA, one instanced draw.
add_executable(njin_crowd main.cpp crowd.cpp dna.cpp sheet.cpp sim.cpp)
target_link_libraries(njin_crowd PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/crowd/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_crowd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/crowd")

njin_add_assets(njin_crowd assets)

njin_icon(njin_crowd)
