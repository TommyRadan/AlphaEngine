$root = "C:\Users\tomor\Documents\Projects\Personal\AlphaEngine"
$dirs = @("rendering_engine","external","infrastructure","event_engine","control","scene_graph","physics_engine","sound_engine","ai_engine","network_engine")

$subs = @(
    @("bufferUsage::","buffer_usage::"),
    @("opengl::Type::","opengl::type::"),
    @("primitive::Triangles","primitive::triangles"),
    @("primitive::Lines","primitive::lines"),
    @("primitive::Points","primitive::points"),
    @("type::UnsignedInt","type::unsigned_int"),
    @("type::UnsignedByte","type::unsigned_byte"),
    @("type::UnsignedShort","type::unsigned_short"),
    @("Type::UnsignedInt","type::unsigned_int"),
    @("Type::UnsignedByte","type::unsigned_byte"),
    @("Type::UnsignedShort","type::unsigned_short"),
    @("Type::Int","type::Int"),
    @("Type::Float","type::Float"),
    @("VertexPostionNormal","vertex_position_normal"),
    @("VertexPositionNormal","vertex_position_normal"),
    @("VertexPositionUv","vertex_position_uv"),
    @("VertexPositionUvNormal","vertex_position_uv_normal"),
    @("m_VertexBuffer","m_vertex_buffer"),
    @("m_IndiciesBuffer","m_indicies_buffer"),
    @("m_VertexArrayObject","m_vertex_array_object"),
    @("m_VertexBufferObject","m_vertex_buffer_object"),
    @("m_VertexCount","m_vertex_count"),
    @("m_CurrentRenderer","m_current_renderer"),
    @("m_CurrentCamera","m_current_camera"),
    @("ConstructProgram(","construct_program("),
    @("DestructProgram(","destruct_program("),
    @(".StartRenderer(",".start_renderer("),
    @(".StopRenderer(",".stop_renderer("),
    @("::StartRenderer(","::start_renderer("),
    @("::StopRenderer(","::stop_renderer(")
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
