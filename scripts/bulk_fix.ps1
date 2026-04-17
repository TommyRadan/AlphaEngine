$root = "C:\Users\tomor\Documents\Projects\Personal\AlphaEngine"
$dirs = @("rendering_engine","external","infrastructure","event_engine","control","scene_graph","physics_engine","sound_engine","ai_engine","network_engine")

# Ordered list of (pattern, replacement) literal substitutions
$subs = @(
    # ---- method/function names (dot/arrow/scope) ----
    @(".Upload(",".upload("),
    @(".Render(",".render("),
    @(".Upload;",".upload;"),
    @(".Render;",".render;"),
    @("->Upload(","->upload("),
    @("->Render(","->render("),
    @("::Upload(","::upload("),
    @("::Render(","::render("),

    @("::DrawArrays(","::draw_arrays("),
    @("::DrawElements(","::draw_elements("),
    @(".DrawArrays(",".draw_arrays("),
    @(".DrawElements(",".draw_elements("),

    @("::CreateVBO(","::create_vbo("),
    @("::CreateVAO(","::create_vao("),
    @(".CreateVBO(",".create_vbo("),
    @(".CreateVAO(",".create_vao("),

    @(".Data(",".data("),
    @("->Data(","->data("),
    @(".ElementData(",".element_data("),
    @("->ElementData(","->element_data("),

    @(".BindAttribute(",".bind_attribute("),
    @(".BindElements(",".bind_elements("),
    @("->BindAttribute(","->bind_attribute("),
    @("->BindElements(","->bind_elements("),

    @(".VertexCount(",".vertex_count("),
    @("->VertexCount(","->vertex_count("),
    @(".Vertices(",".vertices("),
    @("->Vertices(","->vertices("),

    @(".SetPosition(",".set_position("),
    @(".SetRotation(",".set_rotation("),
    @(".SetScale(",".set_scale("),
    @("->SetPosition(","->set_position("),
    @("->SetRotation(","->set_rotation("),
    @("->SetScale(","->set_scale("),
    @(".GetPosition(",".get_position("),
    @(".GetRotation(",".get_rotation("),
    @(".GetScale(",".get_scale("),
    @(".GetTransformMatrix(",".get_transform_matrix("),
    @("->GetTransformMatrix(","->get_transform_matrix("),

    @(".SetPixel(",".set_pixel("),
    @("->SetPixel(","->set_pixel("),
    @(".GetPixel(",".get_pixel("),
    @(".GetWidth(",".get_width("),
    @(".GetHeight(",".get_height("),
    @(".GetPixels(",".get_pixels("),

    @(".UploadMesh(",".upload_mesh("),
    @("->UploadMesh(","->upload_mesh("),
    @(".UploadObj(",".upload_obj("),
    @("->UploadObj(","->upload_obj("),

    @("::UploadMatrix4(","::upload_matrix4("),
    @("::UploadMatrix3(","::upload_matrix3("),
    @("::UploadVector2(","::upload_vector2("),
    @("::UploadVector3(","::upload_vector3("),
    @("::UploadVector4(","::upload_vector4("),
    @("::UploadCoefficient(","::upload_coefficient("),
    @("::UploadTextureReference(","::upload_texture_reference("),
    @(".UploadMatrix4(",".upload_matrix4("),
    @(".UploadMatrix3(",".upload_matrix3("),
    @(".UploadVector2(",".upload_vector2("),
    @(".UploadVector3(",".upload_vector3("),
    @(".UploadVector4(",".upload_vector4("),
    @(".UploadCoefficient(",".upload_coefficient("),
    @(".UploadTextureReference(",".upload_texture_reference("),
    @("->UploadMatrix4(","->upload_matrix4("),
    @("->UploadMatrix3(","->upload_matrix3("),
    @("->UploadVector2(","->upload_vector2("),
    @("->UploadVector3(","->upload_vector3("),
    @("->UploadVector4(","->upload_vector4("),
    @("->UploadCoefficient(","->upload_coefficient("),
    @("->UploadTextureReference(","->upload_texture_reference("),

    @(".StartRenderer(",".start_renderer("),
    @(".StopRenderer(",".stop_renderer("),
    @(".SetupCamera(",".setup_camera("),
    @(".SetupOptions(",".setup_options("),
    @("->StartRenderer(","->start_renderer("),
    @("->StopRenderer(","->stop_renderer("),
    @("->SetupCamera(","->setup_camera("),
    @("->SetupOptions(","->setup_options("),

    @("::GetCurrentCamera(","::get_current_camera("),
    @("::GetCurrentRenderer(","::get_current_renderer("),

    # ---- enum class qualifiers ----
    @("BufferUsage::","buffer_usage::"),
    @("Primitive::","primitive::"),
    @("Capability::","capability::"),
    @("Wrapping::","wrapping::"),
    @("Filter::","filter::"),
    @("ShaderType::","shader_type::"),
    @("InternalFormat::","internal_format::"),
    @("DataType::","data_type::"),
    @("Format::","format::"),
    @("Buffer::","buffer::"),

    # enum values
    @("StaticDraw","static_draw"),
    @("StaticRead","static_read"),
    @("StaticCopy","static_copy"),
    @("DynamicDraw","dynamic_draw"),
    @("DynamicRead","dynamic_read"),
    @("DynamicCopy","dynamic_copy"),
    @("StreamDraw","stream_draw"),
    @("StreamRead","stream_read"),
    @("StreamCopy","stream_copy"),

    # ---- type/base-class references in class declarations ----
    @("public Renderable","public renderable"),
    @("public Renderer","public renderer"),
    @("public Camera","public camera"),
    @(": Renderer{",": renderer{"),
    @(": Renderer {",": renderer {"),
    @(": Camera{",": camera{"),
    @(": Camera {",": camera {"),

    # Color type in rendering_engine::util
    @(" Color ",' color '),
    @(" Color*",' color*'),
    @(" Color&",' color&'),
    @(" Color{",' color{'),
    @("(Color ","(color "),
    @("(Color*","(color*"),
    @("(Color&","(color&"),
    @("(Color{","(color{"),
    @("new Color","new color"),
    @("sizeof(Color)","sizeof(color)"),
    @("return Color(","return color("),
    @(", Color"," , color"),

    # Image type
    @("new Image(","new image("),
    @("= Image(","= image("),

    # Renderer* pointer declarations
    @("Renderer* ","renderer* "),
    @("Camera* ","camera* "),
    @("const Renderer* ","const renderer* "),
    @("const Camera* ","const camera* "),

    # vertex struct
    @("vertexPositionUvNormal","vertex_position_uv_normal")
)

foreach ($d in $dirs) {
    $path = Join-Path $root $d
    if (-not (Test-Path $path)) { continue }
    Get-ChildItem -Path $path -Recurse -Include *.cpp,*.hpp,*.h -File | ForEach-Object {
        $file = $_.FullName
        $orig = [System.IO.File]::ReadAllText($file)
        $text = $orig
        foreach ($pair in $subs) {
            $text = $text.Replace($pair[0], $pair[1])
        }
        if ($text -ne $orig) {
            [System.IO.File]::WriteAllText($file, $text)
            Write-Host "updated $($_.FullName.Substring($root.Length+1))"
        }
    }
}
Write-Host "done."
