static_assert(zmid_content_workspace_allocator::directory_index ==
              paging::supervisor_hole_directory_index);
static_assert(zmid_content_workspace_allocator::directory_index !=
              paging::user_directory_index);

#include "zmid_content_core.inc"
#include "zmid_content_deflate.inc"
#include "zmid_content_wrapped_deflate.inc"
#include "zmid_content_base64.inc"
#include "zmid_content_zip.inc"
#include "zmid_content_policy.inc"
