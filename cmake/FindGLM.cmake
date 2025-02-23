find_path(GLM_INCLUDE_DIR glm.hpp
  PATHS /opt/homebrew/include/glm /usr/local/include/glm
  NO_DEFAULT_PATH
)

if(GLM_INCLUDE_DIR)
  set(GLM_FOUND TRUE)
  message("-- Found GLM: ${GLM_INCLUDE_DIR}")
endif()

mark_as_advanced(GLM_INCLUDE_DIR)
