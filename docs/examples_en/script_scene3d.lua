-- A 3D scene built from Lua: a glTF character under an afternoon sky, shadows, a
-- point light, a ball circling on a spline, and a menu that changes the clip.
local M = {}

function M.on_start(self)
  self.model = njin.model_load("models/hero.glb")
  self.clip = "Idle"
  self.t = 0
  self.speed = 1
  -- A closed path through four points; the spline goes with this entity.
  self.path = njin.spline_create({ { -2, 0.4, -1 }, { 0, 0.4, 1.2 }, { 2, 0.4, -1 }, { 0, 0.4, -2.5 } },
                                 { closed = true, owner = self.entity })
  self.follow = { distance = 0, speed = 1.5, ["end"] = "loop" }
  self.ball = njin.vec3(0, 0.4, 0)
end

function M.on_update(self, dt)
  self.t = self.t + dt * self.speed
  self.ball = njin.spline_follow(self.path, self.follow, dt)
end

function M.on_render(self)
  njin.light3d_set({ shadows = true, shadow_range = 8 })
  njin.begin_3d({ position = { 0, 1.7, 4.2 }, target = { 0, 0.8, 0 }, fovy = 50 })
  njin.draw_sky3d({ hour = 15, weather = "clear" }) -- sets the sun direction and the ambient light too
  njin.draw_plane3d({ 0, 0, 0 }, { 20, 20 }, { 0.55, 0.57, 0.52, 1 })
  if self.model then
    njin.draw_model(self.model, { position = { 0, 0, 0 } }, { anim = self.clip, time = self.t })
  end
  njin.draw_sphere3d(self.ball, 0.25, { 1, 0.3, 0.2, 1 })
  njin.light3d_add({ kind = "point", position = { 1.2, 0.6, 1.2 }, color = { 0.2, 0.5, 1, 1 }, intensity = 4,
                     radius = 3 })
  njin.end_3d()
end

function M.on_ui(self)
  njin.ui_begin({ id = "menu", title = "Character", anchor = { 0, 0 }, pivot = { 0, 0 }, offset = { 16, 16 },
                  width = 240 })
  njin.ui_label("Clip: " .. self.clip)
  if njin.ui_button("Walk") then
    self.clip = "Walk"
  end
  if njin.ui_button("Stand") then
    self.clip = "Idle"
  end
  local _
  _, self.speed = njin.ui_slider("Speed", self.speed, 0, 2)
  njin.ui_end()
end

return M
