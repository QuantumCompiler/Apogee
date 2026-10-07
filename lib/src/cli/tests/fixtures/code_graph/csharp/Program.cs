using System;
using Bank.Models;

namespace Bank
{
    public static class Program
    {
        public static void Main()
        {
            Account account = new Account();
            IAccount view = account;
            account.Deposit(10);
            Console.WriteLine(view.Balance());
        }
    }
}
