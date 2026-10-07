-- Nhân vật platformer điều khiển bằng script: chạy, nhảy, lật sprite, đổi clip.
-- Entity đã có transform, collider, platformer_body, sprite và animator (do C++ tạo).
local Player = {}

function Player:on_start()
  self.coins = 0
  self.jump_sound = njin.sound_load("assets/jump.wav")
end

function Player:on_update(dt)
  local move = 0
  if njin.key_held("left") then move = move - 1 end
  if njin.key_held("right") then move = move + 1 end
  local jump = njin.key_pressed("space")
  njin.platformer_input(self.entity, move, jump, njin.key_held("space"))

  local body = njin.platformer(self.entity)
  if move ~= 0 then njin.sprite_flip(self.entity, move < 0) end
  if not body.grounded then
    njin.anim_play(self.entity, "jump")
  elseif move ~= 0 then
    njin.anim_play(self.entity, "run")
  else
    njin.anim_play(self.entity, "idle")
  end
  if body.jumped then njin.sound_play(self.jump_sound) end
end

function Player:on_render()
  njin.draw_text("xu: " .. self.coins, njin.vec2(8, 8), 16, 1, 1, 1)
end

return Player
