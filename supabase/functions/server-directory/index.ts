import { createClient } from "npm:@supabase/supabase-js@2.105.0";
import { createDirectoryHandler } from "./handler.ts";

const supabaseUrl = Deno.env.get("SUPABASE_URL") ?? "";
const serviceRoleKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? "";
const admin = createClient(supabaseUrl, serviceRoleKey, {
  auth: { persistSession: false, autoRefreshToken: false },
});

Deno.serve(
  createDirectoryHandler({
    rpc: (fn, args) => admin.rpc(fn, args),
    allowPrivateSources: Deno.env.get("DIRECTORY_ALLOW_PRIVATE_SOURCES") === "1",
    hashSalt: Deno.env.get("DIRECTORY_RATE_LIMIT_SALT") || serviceRoleKey || "server-directory",
  }),
);
