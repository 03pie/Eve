/** @file business_throw.cpp
 * @brief 测试专用的违规同步异常，模块/宿主均保留同步展开。
 */
extern "C" void __cdecl cordis_business_throw()
{
    throw 42;
}
