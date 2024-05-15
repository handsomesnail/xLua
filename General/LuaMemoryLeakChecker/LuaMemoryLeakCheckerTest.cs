using UnityEngine;
using System;
using XLua;

public class LuaMemoryLeakCheckerTest : MonoBehaviour
{
    LuaEnv luaenv = new LuaEnv();
    Action update;
    LuaMemoryLeakChecker.Data data = null;

    void Start()
    {
        luaenv.DoString(@"
           local local_leak = {}
           global_leak = { a = {}}
           --global_leak.a.b = global_leak

           local no_leak = { a = {x = 0}, b = {y = 0}}
           
           function make_leak1()
               table.insert(local_leak, 1)
               table.insert(global_leak, {})
           end

           -- 添加全局变量相当于_G泄露
           function make_leak3()
               new_leak = { a = 1 }
           end

           -- 会不断创建并持有新table，但其实没泄漏
           function innocent()
               no_leak.a = {x = 1}
               no_leak.b = {y = 1}
           end
        ", "@leak1.lua");

        luaenv.DoString(@"
           local anthor_leak = {a = {{ b = {}}}}

           function make_leak2()
               table.insert(anthor_leak.a[1].b, 1)
           end

           local t = 1

           slow_global_leak = {}

           debug.getregistry()['ref_anthor_leak'] = anthor_leak
           
           function slow_leak()
               if t == 60 then
                   t = 0
                   table.insert(slow_global_leak, {x = 0, y = 1})
               else
                   t = t + 1
               end
           end
 
        ", "@leak2.lua");

        luaenv.DoString(@"
            shutdown_fast_leak = false

            function update()
                 if not shutdown_fast_leak then
                     make_leak1()
                     make_leak2()
                     -- make_leak3()
                 end
                 innocent()
                 slow_leak()
            end
        ", "@main.lua");

        luaenv.Global.Get("update", out update);

        data = luaenv.StartMemoryLeakCheck();
        Debug.Log("Start, PotentialLeakCount:" + data.PotentialLeakCount);
    }

    int tick = 0;

    bool finished = false;

    void Update()
    {
        if (!finished)
        {
            tick++;
            update();
            luaenv.Tick();

            if (tick % 30 == 0)
            {
                LuaMemoryLeakChecker.Data report = luaenv.MemoryLeakCheck(data);
                Debug.Log("Update, PotentialLeakCount:" + report.PotentialLeakCount);
            }

            if (tick % 180 == 0)
            {
                LuaMemoryLeakChecker.Data report = luaenv.MemoryLeakCheck(data);
                Debug.Log(luaenv.MemoryLeakReport(report));

                if (tick == 180)
                {
                    Debug.Log("shutdown_fast_leak");
                    //假装解决了快速内存泄漏
                    luaenv.Global.Set("shutdown_fast_leak", true);
                    //开启一个新的泄漏检测
                    data = luaenv.StartMemoryLeakCheck();
                }
                else
                {
                    finished = true;
                    Debug.Log("Finished");
                }
            }
        }
    }

    void OnDestroy()
    {
        update = null;
        luaenv.Dispose();
    }
}
