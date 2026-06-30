local semver = require("semver_orig")

local mt = getmetatable(semver)
function mt:__le(other)
  if self.major == other.major and
     self.minor == other.minor and
     self.patch == other.patch and
     self.prerelease == other.prerelease then
	 return true
  end
  if self.major ~= other.major then return self.major < other.major end
  if self.minor ~= other.minor then return self.minor < other.minor end
  if self.patch ~= other.patch then return self.patch < other.patch end
  return smallerPrerelease(self.prerelease, other.prerelease)
end
setmetatable(semver, mt)

return semver
